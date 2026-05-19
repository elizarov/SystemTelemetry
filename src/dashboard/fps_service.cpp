#include "dashboard/fps_service.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <sddl.h>
#include <string>
#include <utility>
#include <vector>
#include <winsvc.h>

#include "telemetry/fps_provider.h"
#include "telemetry/fps_service_protocol.h"
#include "util/command_line.h"
#include "util/paths.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

constexpr DWORD kServiceStartWaitMs = 10000;
constexpr DWORD kServiceStopWaitMs = 10000;
constexpr DWORD kServiceDeleteWaitMs = 5000;
constexpr DWORD kPipeBufferBytes = 4096;
constexpr DWORD kPipeRequestBytes = 128;
constexpr char kFpsServiceDisplayName[] = "CaseDash Service";
constexpr char kPipeSecurityDescriptor[] = "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)";

SERVICE_STATUS_HANDLE g_serviceStatusHandle = nullptr;
SERVICE_STATUS g_serviceStatus{};
HANDLE g_serviceStopEvent = nullptr;
HANDLE g_serviceWorker = nullptr;

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

std::string BuildFpsServiceBinaryPath() {
    const std::optional<FilePath> executablePath = GetExecutablePath();
    if (!executablePath.has_value()) {
        return {};
    }
    return FormatText("%s /service", QuoteCommandLineArgument(executablePath->string()).c_str());
}

DWORD OpenInstalledService(ServiceHandle& manager, DWORD desiredAccess, ServiceHandle& service) {
    manager = ServiceHandle(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (manager.Get() == nullptr) {
        return GetLastError();
    }

    const std::wstring serviceName = WideFromUtf8(kFpsServiceName);
    service = ServiceHandle(OpenServiceW(manager.Get(), serviceName.c_str(), desiredAccess));
    return service.Get() != nullptr ? ERROR_SUCCESS : GetLastError();
}

bool IsServiceAbsentOrPendingDelete(DWORD status) {
    return status == ERROR_SERVICE_DOES_NOT_EXIST || status == ERROR_SERVICE_MARKED_FOR_DELETE;
}

DWORD WaitForServiceDeletedOrPendingDelete() {
    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kServiceDeleteWaitMs) {
        ServiceHandle manager;
        ServiceHandle service;
        const DWORD status = OpenInstalledService(manager, SERVICE_QUERY_STATUS, service);
        if (IsServiceAbsentOrPendingDelete(status)) {
            return ERROR_SUCCESS;
        }
        if (status != ERROR_SUCCESS) {
            return status;
        }
        Sleep(100);
    }
    return ERROR_TIMEOUT;
}

bool IsExpectedServiceBinaryPath(const std::string& command) {
    const std::string expected = BuildFpsServiceBinaryPath();
    return !expected.empty() && NormalizeCommandPath(command) == NormalizeCommandPath(expected);
}

std::optional<std::string> QueryServiceBinaryPath(SC_HANDLE service) {
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
    return Utf8FromWide(config->lpBinaryPathName);
}

DWORD QueryServiceStatusProcess(SC_HANDLE service, SERVICE_STATUS_PROCESS& status) {
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(
            service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
        return GetLastError();
    }
    return ERROR_SUCCESS;
}

DWORD StoppedServiceStatusCode(const SERVICE_STATUS_PROCESS& status) {
    return status.dwWin32ExitCode != NO_ERROR ? status.dwWin32ExitCode : ERROR_SERVICE_NOT_ACTIVE;
}

DWORD ServicePollIntervalMs(const SERVICE_STATUS_PROCESS& status) {
    if (status.dwWaitHint == 0) {
        return 100;
    }
    return std::clamp(status.dwWaitHint / 10, static_cast<DWORD>(100), static_cast<DWORD>(1000));
}

DWORD WaitForServiceState(SC_HANDLE service, DWORD expectedState, DWORD timeoutMs) {
    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < timeoutMs) {
        SERVICE_STATUS_PROCESS status{};
        const DWORD queryStatus = QueryServiceStatusProcess(service, status);
        if (queryStatus != ERROR_SUCCESS) {
            return queryStatus;
        }
        if (status.dwCurrentState == expectedState) {
            return ERROR_SUCCESS;
        }
        if (status.dwCurrentState == SERVICE_STOPPED && expectedState != SERVICE_STOPPED) {
            return StoppedServiceStatusCode(status);
        }
        Sleep(ServicePollIntervalMs(status));
    }

    SERVICE_STATUS_PROCESS status{};
    const DWORD queryStatus = QueryServiceStatusProcess(service, status);
    if (queryStatus != ERROR_SUCCESS) {
        return queryStatus;
    }
    if (status.dwCurrentState == expectedState) {
        return ERROR_SUCCESS;
    }
    if (status.dwCurrentState == SERVICE_STOPPED && expectedState != SERVICE_STOPPED) {
        return StoppedServiceStatusCode(status);
    }
    return ERROR_TIMEOUT;
}

DWORD StartServiceIfNeeded(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    DWORD statusResult = QueryServiceStatusProcess(service, status);
    if (statusResult != ERROR_SUCCESS) {
        return statusResult;
    }
    if (status.dwCurrentState == SERVICE_RUNNING) {
        return ERROR_SUCCESS;
    }
    if (status.dwCurrentState == SERVICE_START_PENDING) {
        return WaitForServiceState(service, SERVICE_RUNNING, kServiceStartWaitMs);
    }
    if (status.dwCurrentState == SERVICE_STOP_PENDING) {
        statusResult = WaitForServiceState(service, SERVICE_STOPPED, kServiceStopWaitMs);
        if (statusResult != ERROR_SUCCESS) {
            return statusResult;
        }
    }

    if (StartServiceW(service, 0, nullptr)) {
        return WaitForServiceState(service, SERVICE_RUNNING, kServiceStartWaitMs);
    }
    const DWORD error = GetLastError();
    if (error == ERROR_SERVICE_ALREADY_RUNNING) {
        return WaitForServiceState(service, SERVICE_RUNNING, kServiceStartWaitMs);
    }
    return error;
}

DWORD StopServiceIfRunning(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    DWORD statusResult = QueryServiceStatusProcess(service, status);
    if (statusResult != ERROR_SUCCESS) {
        return statusResult;
    }
    if (status.dwCurrentState == SERVICE_STOPPED) {
        return ERROR_SUCCESS;
    }
    if (status.dwCurrentState == SERVICE_STOP_PENDING) {
        return WaitForServiceState(service, SERVICE_STOPPED, kServiceStopWaitMs);
    }

    SERVICE_STATUS stopStatus{};
    if (!ControlService(service, SERVICE_CONTROL_STOP, &stopStatus)) {
        const DWORD error = GetLastError();
        if (error != ERROR_SERVICE_NOT_ACTIVE) {
            return error;
        }
    }

    return WaitForServiceState(service, SERVICE_STOPPED, kServiceStopWaitMs);
}

SECURITY_ATTRIBUTES PipeSecurityAttributes(LocalMemory& securityDescriptor) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const std::wstring securityDescriptorText = WideFromUtf8(kPipeSecurityDescriptor);
    ConvertStringSecurityDescriptorToSecurityDescriptorW(
        securityDescriptorText.c_str(), SDDL_REVISION_1, &descriptor, nullptr);
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
    const std::wstring pipeName = WideFromUtf8(kFpsServicePipeName);
    return Handle(CreateNamedPipeW(pipeName.c_str(),
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
    std::vector<char> request;
    request.reserve(kPipeRequestBytes);
    std::optional<CashDashServiceRequest> serviceRequest;
    while (request.size() < kPipeRequestBytes) {
        char buffer[kPipeRequestBytes]{};
        DWORD read = 0;
        if (!ReadFile(pipe, buffer, static_cast<DWORD>(std::size(buffer)), &read, nullptr) || read == 0) {
            return;
        }
        request.insert(request.end(), buffer, buffer + read);

        std::string diagnostics;
        serviceRequest = ParseCashDashServiceRequest(request.data(), request.size(), diagnostics);
        if (serviceRequest.has_value()) {
            break;
        }
        if (diagnostics.find("too short") == std::string::npos &&
            diagnostics.find("payload is malformed") == std::string::npos) {
            return;
        }
    }
    if (!serviceRequest.has_value()) {
        return;
    }

    std::vector<char> response;
    switch (serviceRequest->id) {
        case CashDashServiceRequestId::PresentedFpsSample:
            response = SerializeFpsServiceSample(fpsProvider.Sample());
            break;
    }
    if (response.empty()) {
        return;
    }

    DWORD written = 0;
    WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &written, nullptr);
    FlushFileBuffers(pipe);
}

void RunPipeServer(HANDLE stopEvent) {
    Trace trace;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider = CreateFpsServiceTelemetryProvider(trace);
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

DWORD WINAPI ServiceWorkerThread(void* context) {
    RunPipeServer(static_cast<HANDLE>(context));
    return 0;
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
    const std::wstring serviceName = WideFromUtf8(kFpsServiceName);
    g_serviceStatusHandle = RegisterServiceCtrlHandlerW(serviceName.c_str(), ServiceControlHandler);
    if (g_serviceStatusHandle == nullptr) {
        return;
    }

    SetServiceStatusState(SERVICE_START_PENDING, NO_ERROR, 3000);
    g_serviceStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_serviceStopEvent == nullptr) {
        SetServiceStatusState(SERVICE_STOPPED, GetLastError());
        return;
    }

    g_serviceWorker = CreateThread(nullptr, 0, ServiceWorkerThread, g_serviceStopEvent, 0, nullptr);
    if (g_serviceWorker == nullptr) {
        SetServiceStatusState(SERVICE_STOPPED, GetLastError());
        CloseHandle(g_serviceStopEvent);
        g_serviceStopEvent = nullptr;
        return;
    }
    SetServiceStatusState(SERVICE_RUNNING);

    WaitForSingleObject(g_serviceStopEvent, INFINITE);
    if (g_serviceWorker != nullptr) {
        WaitForSingleObject(g_serviceWorker, INFINITE);
        CloseHandle(g_serviceWorker);
        g_serviceWorker = nullptr;
    }
    CloseHandle(g_serviceStopEvent);
    g_serviceStopEvent = nullptr;
    SetServiceStatusState(SERVICE_STOPPED);
}

}  // namespace

bool IsFpsServiceCommandLine(const CommandLineArguments& commandLine) {
    return HasSwitch(commandLine, "/service");
}

int RunFpsServiceMode() {
    std::wstring serviceName = WideFromUtf8(kFpsServiceName);
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        {serviceName.data(), ServiceMain},
        {nullptr, nullptr},
    };
    return StartServiceCtrlDispatcherW(serviceTable) ? 0 : 1;
}

DWORD InstallOrUpdateFpsService() {
    const std::string binaryPath = BuildFpsServiceBinaryPath();
    if (binaryPath.empty()) {
        return ERROR_FILE_NOT_FOUND;
    }
    const std::wstring wideBinaryPath = WideFromUtf8(binaryPath);
    const std::wstring serviceName = WideFromUtf8(kFpsServiceName);
    const std::wstring serviceDisplayName = WideFromUtf8(kFpsServiceDisplayName);
    constexpr DWORD kServiceAccess =
        SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP;

    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
    if (manager.Get() == nullptr) {
        return GetLastError();
    }

    ServiceHandle service(CreateServiceW(manager.Get(),
        serviceName.c_str(),
        serviceDisplayName.c_str(),
        kServiceAccess,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        wideBinaryPath.c_str(),
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
        service = ServiceHandle(OpenServiceW(manager.Get(), serviceName.c_str(), kServiceAccess));
        if (service.Get() == nullptr) {
            return GetLastError();
        }
        const std::optional<std::string> previousBinaryPath = QueryServiceBinaryPath(service.Get());
        const bool binaryPathChanged =
            !previousBinaryPath.has_value() || !IsExpectedServiceBinaryPath(*previousBinaryPath);
        if (!ChangeServiceConfigW(service.Get(),
                SERVICE_WIN32_OWN_PROCESS,
                SERVICE_AUTO_START,
                SERVICE_ERROR_NORMAL,
                wideBinaryPath.c_str(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                serviceDisplayName.c_str())) {
            return GetLastError();
        }
        if (binaryPathChanged) {
            const DWORD stopStatus = StopServiceIfRunning(service.Get());
            if (stopStatus != ERROR_SUCCESS && stopStatus != ERROR_SERVICE_NOT_ACTIVE) {
                return stopStatus;
            }
        }
    }

    return StartServiceIfNeeded(service.Get());
}

DWORD StopAndDeleteFpsService() {
    ServiceHandle manager;
    ServiceHandle service;
    DWORD status = OpenInstalledService(manager, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS, service);
    if (IsServiceAbsentOrPendingDelete(status)) {
        return ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS) {
        return status;
    }

    (void)StopServiceIfRunning(service.Get());
    // SCM stop completion can lag; still request deletion so a late stop cannot leave auto-start installed.
    if (!DeleteService(service.Get())) {
        const DWORD deleteError = GetLastError();
        return deleteError == ERROR_SERVICE_MARKED_FOR_DELETE ? ERROR_SUCCESS : deleteError;
    }
    service = ServiceHandle();
    return WaitForServiceDeletedOrPendingDelete();
}

bool IsFpsServiceRunningForCurrentExecutable() {
    ServiceHandle manager;
    ServiceHandle service;
    const DWORD status = OpenInstalledService(manager, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS, service);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    const std::optional<std::string> binaryPath = QueryServiceBinaryPath(service.Get());
    if (!binaryPath.has_value() || !IsExpectedServiceBinaryPath(*binaryPath)) {
        return false;
    }

    SERVICE_STATUS_PROCESS serviceStatus{};
    if (QueryServiceStatusProcess(service.Get(), serviceStatus) != ERROR_SUCCESS) {
        return false;
    }
    return serviceStatus.dwCurrentState == SERVICE_RUNNING;
}
