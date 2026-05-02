#include "main/fps_service.h"

#include <iterator>
#include <memory>
#include <optional>
#include <sddl.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <winsvc.h>

#include "diagnostics/diagnostics.h"
#include "telemetry/fps/fps_etw_provider.h"
#include "telemetry/fps/fps_service_protocol.h"
#include "util/command_line.h"
#include "util/paths.h"
#include "util/trace.h"

namespace {

constexpr DWORD kServiceStopWaitMs = 10000;
constexpr DWORD kPipeBufferBytes = 4096;

SERVICE_STATUS_HANDLE g_serviceStatusHandle = nullptr;
SERVICE_STATUS g_serviceStatus{};
HANDLE g_serviceStopEvent = nullptr;
std::thread g_serviceWorker;

class Handle {
public:
    explicit Handle(HANDLE handle = nullptr) : handle_(handle) {}

    ~Handle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    HANDLE Get() const {
        return handle_;
    }

    HANDLE Release() {
        HANDLE handle = handle_;
        handle_ = nullptr;
        return handle;
    }

private:
    HANDLE handle_ = nullptr;
};

class ServiceHandle {
public:
    explicit ServiceHandle(SC_HANDLE handle = nullptr) : handle_(handle) {}

    ~ServiceHandle() {
        if (handle_ != nullptr) {
            CloseServiceHandle(handle_);
        }
    }

    ServiceHandle(const ServiceHandle&) = delete;
    ServiceHandle& operator=(const ServiceHandle&) = delete;

    ServiceHandle(ServiceHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    ServiceHandle& operator=(ServiceHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr) {
                CloseServiceHandle(handle_);
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    SC_HANDLE Get() const {
        return handle_;
    }

private:
    SC_HANDLE handle_ = nullptr;
};

class LocalMemory {
public:
    explicit LocalMemory(void* memory = nullptr) : memory_(memory) {}

    ~LocalMemory() {
        if (memory_ != nullptr) {
            LocalFree(memory_);
        }
    }

    LocalMemory(const LocalMemory&) = delete;
    LocalMemory& operator=(const LocalMemory&) = delete;

    LocalMemory(LocalMemory&& other) noexcept : memory_(std::exchange(other.memory_, nullptr)) {}

    LocalMemory& operator=(LocalMemory&& other) noexcept {
        if (this != &other) {
            if (memory_ != nullptr) {
                LocalFree(memory_);
            }
            memory_ = std::exchange(other.memory_, nullptr);
        }
        return *this;
    }

    void* Get() const {
        return memory_;
    }

private:
    void* memory_ = nullptr;
};

void SetServiceStatusState(DWORD state, DWORD win32ExitCode = NO_ERROR, DWORD waitHint = 0) {
    if (g_serviceStatusHandle == nullptr) {
        return;
    }

    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwCurrentState = state;
    g_serviceStatus.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    g_serviceStatus.dwWin32ExitCode = win32ExitCode;
    g_serviceStatus.dwWaitHint = waitHint;
    SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

std::wstring BuildFpsServiceBinaryPath() {
    const std::optional<std::wstring> executablePath = GetExecutablePath();
    if (!executablePath.has_value()) {
        return {};
    }
    return QuoteCommandLineArgument(*executablePath) + L" /service";
}

DWORD OpenInstalledService(ServiceHandle& manager, DWORD desiredAccess, ServiceHandle& service) {
    manager = ServiceHandle(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (manager.Get() == nullptr) {
        return GetLastError();
    }

    service = ServiceHandle(OpenServiceW(manager.Get(), kFpsServiceName, desiredAccess));
    return service.Get() != nullptr ? ERROR_SUCCESS : GetLastError();
}

bool IsExpectedServiceBinaryPath(const std::wstring& command) {
    const std::wstring expected = BuildFpsServiceBinaryPath();
    return !expected.empty() && NormalizeWindowsPath(command) == NormalizeWindowsPath(expected);
}

std::optional<std::wstring> QueryServiceBinaryPath(SC_HANDLE service) {
    DWORD bytesNeeded = 0;
    QueryServiceConfigW(service, nullptr, 0, &bytesNeeded);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytesNeeded == 0) {
        return std::nullopt;
    }

    std::vector<BYTE> buffer(bytesNeeded);
    auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    if (!QueryServiceConfigW(service, config, static_cast<DWORD>(buffer.size()), &bytesNeeded) ||
        config->lpBinaryPathName == nullptr) {
        return std::nullopt;
    }
    return std::wstring(config->lpBinaryPathName);
}

DWORD StartServiceIfNeeded(SC_HANDLE service) {
    if (StartServiceW(service, 0, nullptr)) {
        return ERROR_SUCCESS;
    }
    const DWORD error = GetLastError();
    return error == ERROR_SERVICE_ALREADY_RUNNING ? ERROR_SUCCESS : error;
}

DWORD StopServiceIfRunning(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(
            service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
        return GetLastError();
    }
    if (status.dwCurrentState == SERVICE_STOPPED) {
        return ERROR_SUCCESS;
    }

    SERVICE_STATUS stopStatus{};
    if (!ControlService(service, SERVICE_CONTROL_STOP, &stopStatus)) {
        const DWORD error = GetLastError();
        if (error != ERROR_SERVICE_NOT_ACTIVE) {
            return error;
        }
    }

    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kServiceStopWaitMs) {
        if (!QueryServiceStatusEx(
                service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
            return GetLastError();
        }
        if (status.dwCurrentState == SERVICE_STOPPED) {
            return ERROR_SUCCESS;
        }
        Sleep(100);
    }
    return ERROR_TIMEOUT;
}

SECURITY_ATTRIBUTES PipeSecurityAttributes(LocalMemory& securityDescriptor) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)", SDDL_REVISION_1, &descriptor, nullptr);
    securityDescriptor = LocalMemory(descriptor);

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = securityDescriptor.Get();
    attributes.bInheritHandle = FALSE;
    return attributes;
}

Handle CreateFpsPipeInstance() {
    LocalMemory securityDescriptor;
    SECURITY_ATTRIBUTES securityAttributes = PipeSecurityAttributes(securityDescriptor);
    return Handle(CreateNamedPipeW(kFpsServicePipeName,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        kPipeBufferBytes,
        kPipeBufferBytes,
        0,
        securityAttributes.lpSecurityDescriptor != nullptr ? &securityAttributes : nullptr));
}

bool ConnectPipeOrStop(HANDLE pipe, HANDLE stopEvent) {
    Handle connectedEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (connectedEvent.Get() == nullptr) {
        return false;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = connectedEvent.Get();
    if (ConnectNamedPipe(pipe, &overlapped)) {
        return true;
    }

    const DWORD error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED) {
        return true;
    }
    if (error != ERROR_IO_PENDING) {
        return false;
    }

    HANDLE waitHandles[] = {stopEvent, connectedEvent.Get()};
    const DWORD waitResult =
        WaitForMultipleObjects(static_cast<DWORD>(std::size(waitHandles)), waitHandles, FALSE, INFINITE);
    if (waitResult == WAIT_OBJECT_0) {
        CancelIo(pipe);
        return false;
    }
    return waitResult == WAIT_OBJECT_0 + 1;
}

void ServePipeClient(HANDLE pipe, FpsTelemetryProvider& fpsProvider) {
    char request[128]{};
    DWORD read = 0;
    if (!ReadFile(pipe, request, static_cast<DWORD>(std::size(request)), &read, nullptr) ||
        !IsFpsServiceRequest(request, read)) {
        return;
    }

    const FpsTelemetrySample sample = fpsProvider.Sample();
    const std::vector<char> response = SerializeFpsServiceSample(sample);
    DWORD written = 0;
    WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &written, nullptr);
    FlushFileBuffers(pipe);
}

void RunPipeServer(HANDLE stopEvent) {
    Trace trace;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider = CreatePresentedFpsEtwProvider(trace);
    if (fpsProvider != nullptr) {
        fpsProvider->Initialize();
    }

    while (WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT) {
        Handle pipe = CreateFpsPipeInstance();
        if (pipe.Get() == INVALID_HANDLE_VALUE) {
            Sleep(250);
            continue;
        }

        if (ConnectPipeOrStop(pipe.Get(), stopEvent)) {
            if (fpsProvider != nullptr) {
                ServePipeClient(pipe.Get(), *fpsProvider);
            }
            DisconnectNamedPipe(pipe.Get());
        }
    }
}

void WINAPI ServiceControlHandler(DWORD control) {
    if (control != SERVICE_CONTROL_STOP && control != SERVICE_CONTROL_SHUTDOWN) {
        return;
    }

    SetServiceStatusState(SERVICE_STOP_PENDING, NO_ERROR, 3000);
    if (g_serviceStopEvent != nullptr) {
        SetEvent(g_serviceStopEvent);
    }
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_serviceStatusHandle = RegisterServiceCtrlHandlerW(kFpsServiceName, ServiceControlHandler);
    if (g_serviceStatusHandle == nullptr) {
        return;
    }

    SetServiceStatusState(SERVICE_START_PENDING, NO_ERROR, 3000);
    g_serviceStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_serviceStopEvent == nullptr) {
        SetServiceStatusState(SERVICE_STOPPED, GetLastError());
        return;
    }

    g_serviceWorker = std::thread([] { RunPipeServer(g_serviceStopEvent); });
    SetServiceStatusState(SERVICE_RUNNING);

    WaitForSingleObject(g_serviceStopEvent, INFINITE);
    if (g_serviceWorker.joinable()) {
        g_serviceWorker.join();
    }
    CloseHandle(g_serviceStopEvent);
    g_serviceStopEvent = nullptr;
    SetServiceStatusState(SERVICE_STOPPED);
}

}  // namespace

bool IsFpsServiceCommandLine() {
    return HasSwitch("/service");
}

int RunFpsServiceMode() {
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        {const_cast<LPWSTR>(kFpsServiceName), ServiceMain},
        {nullptr, nullptr},
    };
    return StartServiceCtrlDispatcherW(serviceTable) ? 0 : 1;
}

DWORD InstallOrUpdateFpsService() {
    const std::wstring binaryPath = BuildFpsServiceBinaryPath();
    if (binaryPath.empty()) {
        return ERROR_FILE_NOT_FOUND;
    }

    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
    if (manager.Get() == nullptr) {
        return GetLastError();
    }

    ServiceHandle service(CreateServiceW(manager.Get(),
        kFpsServiceName,
        L"System Telemetry FPS Service",
        SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binaryPath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr));

    if (service.Get() == nullptr) {
        const DWORD createError = GetLastError();
        if (createError != ERROR_SERVICE_EXISTS) {
            return createError;
        }
        service = ServiceHandle(
            OpenServiceW(manager.Get(), kFpsServiceName, SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START));
        if (service.Get() == nullptr) {
            return GetLastError();
        }
        if (!ChangeServiceConfigW(service.Get(),
                SERVICE_WIN32_OWN_PROCESS,
                SERVICE_AUTO_START,
                SERVICE_ERROR_NORMAL,
                binaryPath.c_str(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                L"System Telemetry FPS Service")) {
            return GetLastError();
        }
    }

    return StartServiceIfNeeded(service.Get());
}

DWORD StopAndDeleteFpsService() {
    ServiceHandle manager;
    ServiceHandle service;
    DWORD status = OpenInstalledService(manager, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS, service);
    if (status == ERROR_SERVICE_DOES_NOT_EXIST) {
        return ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS) {
        return status;
    }

    status = StopServiceIfRunning(service.Get());
    if (status != ERROR_SUCCESS && status != ERROR_SERVICE_NOT_ACTIVE) {
        return status;
    }
    if (!DeleteService(service.Get())) {
        const DWORD deleteError = GetLastError();
        return deleteError == ERROR_SERVICE_MARKED_FOR_DELETE ? ERROR_SUCCESS : deleteError;
    }
    return ERROR_SUCCESS;
}

bool IsFpsServiceInstalledForCurrentExecutable() {
    ServiceHandle manager;
    ServiceHandle service;
    const DWORD status = OpenInstalledService(manager, SERVICE_QUERY_CONFIG, service);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    const std::optional<std::wstring> binaryPath = QueryServiceBinaryPath(service.Get());
    return binaryPath.has_value() && IsExpectedServiceBinaryPath(*binaryPath);
}
