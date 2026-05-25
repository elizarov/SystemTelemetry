#include "telemetry/board/lenovo/board_lenovo_vantage.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <Wbemidl.h>

#include "telemetry/fps_service_protocol.h"
#include "telemetry/impl/system_info_support.h"
#include "util/elevated_process.h"
#include "util/file_path.h"
#include "util/resource_strings.h"
#include "util/strings.h"
#include "util/text_encoding.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/win32_format.h"

namespace {

constexpr char kLenovoProviderName[] = "Lenovo";
constexpr char kLenovoDirectDriverLibrary[] = "Lenovo Diagnostics Driver";
constexpr char kLenovoDiagnosticsDriverServiceDll[] = "LenovoDiagnosticsDriverService.dll";
constexpr char kLenovoDiagnosticsDriverSys[] = "LenovoDiagnosticsDriver.sys";
constexpr char kLenovoCpuTemperatureName[] = "CPU Temperature";
constexpr DWORD kPipeConnectTimeoutMs = 100;
constexpr DWORD kPipeReadChunkBytes = 4096;
constexpr DWORD kMaximumPipeResponseBytes = 16 * 1024;
constexpr int kSensorRetrySampleInterval = 10;
constexpr DWORD kWmiQueryTimeoutMs = 1500;
constexpr char kLenovoDiagnosticsDriverServiceName[] = "LenovoDiagnosticsDriver";
constexpr wchar_t kLenovoGameZoneNamespace[] = L"ROOT\\WMI";         // WMI COM BSTR boundary has no A API.
constexpr wchar_t kLenovoGameZoneClass[] = L"LENOVO_GAMEZONE_DATA";  // WMI COM BSTR boundary has no A API.
constexpr wchar_t kWmiRelPathProperty[] = L"__RELPATH";              // WMI property boundary has no A API.
constexpr wchar_t kWmiDataProperty[] = L"Data";                      // WMI property boundary has no A API.
constexpr wchar_t kGetFanCountMethod[] = L"GetFanCount";             // WMI method boundary has no A API.
constexpr wchar_t kGetFan1SpeedMethod[] = L"GetFan1Speed";           // WMI method boundary has no A API.
constexpr wchar_t kGetFan2SpeedMethod[] = L"GetFan2Speed";           // WMI method boundary has no A API.

struct LenovoSensorSnapshot {
    bool success = false;
    std::string diagnostics;
    std::string driverLibrary = kLenovoDirectDriverLibrary;
    std::vector<BoardSensorReading> fans;
    std::vector<BoardSensorReading> temperatures;
};

struct LenovoGameZoneFanValue {
    std::uint32_t rpm = 0;
};

class Handle {
public:
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}

    ~Handle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HANDLE Get() const {
        return handle_;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class ServiceHandle {
public:
    explicit ServiceHandle(SC_HANDLE handle = nullptr) : handle_(handle) {}

    ~ServiceHandle() {
        Reset();
    }

    ServiceHandle(const ServiceHandle&) = delete;
    ServiceHandle& operator=(const ServiceHandle&) = delete;

    SC_HANDLE Get() const {
        return handle_;
    }

    bool Valid() const {
        return handle_ != nullptr;
    }

    void Reset(SC_HANDLE handle = nullptr) {
        if (handle_ != nullptr) {
            CloseServiceHandle(handle_);
        }
        handle_ = handle;
    }

private:
    SC_HANDLE handle_ = nullptr;
};

class LibraryHandle {
public:
    explicit LibraryHandle(HMODULE handle = nullptr) : handle_(handle) {}

    ~LibraryHandle() {
        Reset();
    }

    LibraryHandle(const LibraryHandle&) = delete;
    LibraryHandle& operator=(const LibraryHandle&) = delete;

    HMODULE Get() const {
        return handle_;
    }

    bool Valid() const {
        return handle_ != nullptr;
    }

    void Reset(HMODULE handle = nullptr) {
        if (handle_ != nullptr) {
            FreeLibrary(handle_);
        }
        handle_ = handle;
    }

private:
    HMODULE handle_ = nullptr;
};

class ComApartment {
public:
    ComApartment() : status_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        uninitialize_ = SUCCEEDED(status_);
    }

    ~ComApartment() {
        if (uninitialize_) {
            CoUninitialize();
        }
    }

    bool Ready() const {
        return SUCCEEDED(status_) || status_ == RPC_E_CHANGED_MODE;
    }

    HRESULT Status() const {
        return status_;
    }

private:
    HRESULT status_ = E_FAIL;
    bool uninitialize_ = false;
};

class Bstr {
public:
    explicit Bstr(const wchar_t* value) : value_(SysAllocString(value)) {}

    ~Bstr() {
        SysFreeString(value_);
    }

    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;

    BSTR Get() const {
        return value_;
    }

    bool Valid() const {
        return value_ != nullptr;
    }

private:
    BSTR value_ = nullptr;
};

template <typename T> class ComObject {
public:
    ComObject() = default;

    ~ComObject() {
        Reset();
    }

    ComObject(const ComObject&) = delete;
    ComObject& operator=(const ComObject&) = delete;

    T* Get() const {
        return value_;
    }

    T** Out() {
        Reset();
        return &value_;
    }

    void Reset() {
        if (value_ != nullptr) {
            value_->Release();
            value_ = nullptr;
        }
    }

private:
    T* value_ = nullptr;
};

std::string TextFromNullableWide(const wchar_t* text) {
    return text != nullptr ? TextFromWide(text) : std::string();
}

std::string FormatHresult(HRESULT value) {
    std::string text;
    AppendHresult(text, value);
    return text;
}

bool IsSaneRpm(double value) {
    return value > 0.0 && value < 30000.0;
}

bool IsSaneCelsius(double value) {
    return value > 0.0 && value <= 125.0;
}

bool HasAvailableFanReading(const LenovoSensorSnapshot& snapshot) {
    return std::any_of(snapshot.fans.begin(), snapshot.fans.end(), [](const BoardSensorReading& reading) {
        return reading.value.has_value() && IsSaneRpm(*reading.value);
    });
}

bool DirectoryExists(const FilePath& path) {
    const DWORD attributes = GetFileAttributesA(path.string().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::optional<FilePath> ProgramDataDirectory() {
    std::array<char, MAX_PATH> buffer{};
    const DWORD length = GetEnvironmentVariableA("ProgramData", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::nullopt;
    }
    return FilePath(std::string(buffer.data(), length));
}

std::vector<int> ParseVersionParts(const std::string& text) {
    std::vector<int> parts;
    int value = 0;
    bool hasDigits = false;
    for (const char ch : text) {
        if (ch >= '0' && ch <= '9') {
            value = value * 10 + static_cast<int>(ch - '0');
            hasDigits = true;
            continue;
        }
        if (ch == '.' && hasDigits) {
            parts.push_back(value);
            value = 0;
            hasDigits = false;
            continue;
        }
        return {};
    }
    if (hasDigits) {
        parts.push_back(value);
    }
    return parts;
}

bool VersionGreater(const std::string& left, const std::string& right) {
    const std::vector<int> leftParts = ParseVersionParts(left);
    const std::vector<int> rightParts = ParseVersionParts(right);
    const size_t count = std::max(leftParts.size(), rightParts.size());
    for (size_t i = 0; i < count; ++i) {
        const int leftValue = i < leftParts.size() ? leftParts[i] : 0;
        const int rightValue = i < rightParts.size() ? rightParts[i] : 0;
        if (leftValue != rightValue) {
            return leftValue > rightValue;
        }
    }
    return false;
}

bool IsLenovoDiagnosticsDriverDirectory(const FilePath& path) {
    return DirectoryExists(path) && FileExists(path / kLenovoDiagnosticsDriverSys) &&
           FileExists(path / kLenovoDiagnosticsDriverServiceDll);
}

std::optional<FilePath> FindInstalledLenovoDiagnosticsDriverDirectory() {
    const std::optional<FilePath> programData = ProgramDataDirectory();
    if (!programData.has_value()) {
        return std::nullopt;
    }

    const FilePath addinRoot = *programData / "Lenovo" / "Vantage" / "Addins" / "LenovoHardwareScanAddin";
    if (!DirectoryExists(addinRoot)) {
        return std::nullopt;
    }

    const std::string pattern = (addinRoot / "*").string();
    WIN32_FIND_DATAA findData{};
    HANDLE search = FindFirstFileA(pattern.c_str(), &findData);
    if (search == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    std::optional<FilePath> bestPath;
    std::string bestVersion;
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            continue;
        }
        const std::string name = findData.cFileName;
        if (name == "." || name == "..") {
            continue;
        }
        const FilePath candidate = addinRoot / FilePath(name);
        if (!IsLenovoDiagnosticsDriverDirectory(candidate)) {
            continue;
        }
        if (!bestPath.has_value() || VersionGreater(name, bestVersion)) {
            bestPath = candidate;
            bestVersion = name;
        }
    } while (FindNextFileA(search, &findData));

    FindClose(search);
    return bestPath;
}

bool IsIntelCpuVendor() {
    std::array<int, 4> registers{};
    __cpuid(registers.data(), 0);

    std::array<char, 13> vendor{};
    memcpy(vendor.data(), &registers[1], sizeof(int));
    memcpy(vendor.data() + 4, &registers[3], sizeof(int));
    memcpy(vendor.data() + 8, &registers[2], sizeof(int));
    return std::string(vendor.data()) == "GenuineIntel";
}

bool QueryServiceRunning(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(
            service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
        return false;
    }
    return status.dwCurrentState == SERVICE_RUNNING;
}

bool WaitForServiceRunning(SC_HANDLE service) {
    for (int retry = 0; retry < 50; ++retry) {
        if (QueryServiceRunning(service)) {
            return true;
        }
        Sleep(20);
    }
    return false;
}

void StopServiceBestEffort(SC_HANDLE service) {
    if (service == nullptr) {
        return;
    }

    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    for (int retry = 0; retry < 50; ++retry) {
        SERVICE_STATUS_PROCESS processStatus{};
        DWORD bytesNeeded = 0;
        if (!QueryServiceStatusEx(service,
                SC_STATUS_PROCESS_INFO,
                reinterpret_cast<LPBYTE>(&processStatus),
                sizeof(processStatus),
                &bytesNeeded)) {
            break;
        }
        if (processStatus.dwCurrentState == SERVICE_STOPPED) {
            break;
        }
        Sleep(100);
    }
}

std::optional<double> DecodeIntelMsrTemperature(std::uint64_t thermalStatus, std::uint64_t temperatureTarget) {
    if ((thermalStatus & (std::uint64_t{1} << 31)) == 0) {
        return std::nullopt;
    }

    const std::uint32_t thermalDelta = static_cast<std::uint32_t>((thermalStatus >> 16) & 0x7f);
    const std::uint32_t targetCelsius = static_cast<std::uint32_t>((temperatureTarget >> 16) & 0xff);
    if (targetCelsius <= thermalDelta) {
        return std::nullopt;
    }

    const double celsius = static_cast<double>(targetCelsius - thermalDelta);
    return IsSaneCelsius(celsius) ? std::optional<double>(celsius) : std::nullopt;
}

std::vector<GROUP_AFFINITY> ActiveLogicalProcessorAffinities() {
    std::vector<GROUP_AFFINITY> affinities;
    const WORD groupCount = GetActiveProcessorGroupCount();
    for (WORD group = 0; group < groupCount; ++group) {
        const DWORD processorCount = GetActiveProcessorCount(group);
        const DWORD maximumBits = static_cast<DWORD>(sizeof(KAFFINITY) * 8);
        for (DWORD index = 0; index < processorCount && index < maximumBits; ++index) {
            GROUP_AFFINITY affinity{};
            affinity.Group = group;
            affinity.Mask = static_cast<KAFFINITY>(1) << index;
            affinities.push_back(affinity);
        }
    }
    return affinities;
}

using CreateLDDServiceFn = void* (*)();
using DestroyLDDServiceFn = void (*)(void*);
using LDDServiceBoolMethod = bool (*)(void*);
using LDDServiceReadMsrMethod = int (*)(void*, std::uint32_t, std::uint64_t*);

std::optional<double> ReadLenovoDriverIntelTemperature(void* service, LDDServiceReadMsrMethod readMsr) {
    std::uint64_t thermalStatus = 0;
    std::uint64_t temperatureTarget = 0;
    if (readMsr(service, 0x19c, &thermalStatus) != 0 || readMsr(service, 0x1a2, &temperatureTarget) != 0) {
        return std::nullopt;
    }
    return DecodeIntelMsrTemperature(thermalStatus, temperatureTarget);
}

std::vector<double> ReadLenovoDriverIntelTemperatures(void* service, LDDServiceReadMsrMethod readMsr) {
    std::vector<double> temperatures;
    const std::vector<GROUP_AFFINITY> affinities = ActiveLogicalProcessorAffinities();
    for (const GROUP_AFFINITY& affinity : affinities) {
        GROUP_AFFINITY previousAffinity{};
        const bool affinitySet = SetThreadGroupAffinity(GetCurrentThread(), &affinity, &previousAffinity) != FALSE;
        const std::optional<double> temperature = ReadLenovoDriverIntelTemperature(service, readMsr);
        if (affinitySet) {
            SetThreadGroupAffinity(GetCurrentThread(), &previousAffinity, nullptr);
        }
        if (temperature.has_value()) {
            temperatures.push_back(*temperature);
        }
    }

    if (temperatures.empty()) {
        const std::optional<double> temperature = ReadLenovoDriverIntelTemperature(service, readMsr);
        if (temperature.has_value()) {
            temperatures.push_back(*temperature);
        }
    }
    return temperatures;
}

LenovoSensorSnapshot CaptureLenovoDriverCpuTemperatureSensors(Trace& trace, const FilePath& addinDirectory) {
    LenovoSensorSnapshot snapshot;
    if (!IsIntelCpuVendor()) {
        snapshot.diagnostics =
            ResourceStringText(RES_STR("Lenovo Diagnostics Driver CPU temperature path supports Intel CPUs only."));
        return snapshot;
    }
    if (!IsLenovoDiagnosticsDriverDirectory(addinDirectory)) {
        snapshot.diagnostics =
            ResourceStringText(RES_STR("Lenovo Diagnostics Driver files were not found in the Vantage addin."));
        return snapshot;
    }

    bool serviceCreated = false;
    void* wrapperService = nullptr;
    DestroyLDDServiceFn destroyService = nullptr;
    std::string diagnostics;
    std::vector<double> temperatures;

    ServiceHandle scm(OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
    ServiceHandle service;
    LibraryHandle wrapper;

    do {
        if (!scm.Valid()) {
            diagnostics = FormatText(
                RES_STR("Lenovo Diagnostics Driver SCM open failed: %s"), FormatWin32Error(GetLastError()).c_str());
            break;
        }

        service.Reset(OpenServiceA(scm.Get(),
            kLenovoDiagnosticsDriverServiceName,
            SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | DELETE));
        if (!service.Valid()) {
            const FilePath driverPath = addinDirectory / kLenovoDiagnosticsDriverSys;
            const std::string driverPathText = driverPath.string();
            service.Reset(CreateServiceA(scm.Get(),
                kLenovoDiagnosticsDriverServiceName,
                kLenovoDiagnosticsDriverServiceName,
                SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | DELETE,
                SERVICE_KERNEL_DRIVER,
                SERVICE_DEMAND_START,
                SERVICE_ERROR_NORMAL,
                driverPathText.c_str(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr));
            serviceCreated = service.Valid();
            if (!service.Valid()) {
                diagnostics = FormatText(RES_STR("Lenovo Diagnostics Driver service creation failed: %s"),
                    FormatWin32Error(GetLastError()).c_str());
                break;
            }
        }

        if (!StartServiceA(service.Get(), 0, nullptr)) {
            const DWORD error = GetLastError();
            if (error != ERROR_SERVICE_ALREADY_RUNNING) {
                diagnostics =
                    FormatText(RES_STR("Lenovo Diagnostics Driver start failed: %s"), FormatWin32Error(error).c_str());
                break;
            }
        }
        if (!WaitForServiceRunning(service.Get())) {
            diagnostics = ResourceStringText(RES_STR("Lenovo Diagnostics Driver service did not reach running state."));
            break;
        }

        const FilePath wrapperPath = addinDirectory / kLenovoDiagnosticsDriverServiceDll;
        wrapper.Reset(LoadLibraryA(wrapperPath.string().c_str()));
        if (!wrapper.Valid()) {
            diagnostics = FormatText(
                RES_STR("Lenovo Diagnostics Driver wrapper load failed: %s"), FormatWin32Error(GetLastError()).c_str());
            break;
        }

        const auto createService =
            reinterpret_cast<CreateLDDServiceFn>(GetProcAddress(wrapper.Get(), "CreateLDDService"));
        destroyService = reinterpret_cast<DestroyLDDServiceFn>(GetProcAddress(wrapper.Get(), "DestroyLDDService"));
        if (createService == nullptr || destroyService == nullptr) {
            diagnostics = ResourceStringText(RES_STR("Lenovo Diagnostics Driver wrapper exports were not found."));
            break;
        }

        wrapperService = createService();
        if (wrapperService == nullptr) {
            diagnostics = ResourceStringText(RES_STR("Lenovo Diagnostics Driver wrapper creation failed."));
            break;
        }

        void** vtable = *reinterpret_cast<void***>(wrapperService);
        if (vtable == nullptr || vtable[5] == nullptr || vtable[11] == nullptr) {
            diagnostics = ResourceStringText(RES_STR("Lenovo Diagnostics Driver wrapper ABI was not recognized."));
            break;
        }

        const auto isOpen = reinterpret_cast<LDDServiceBoolMethod>(vtable[5]);
        if (!isOpen(wrapperService)) {
            diagnostics = ResourceStringText(RES_STR("Lenovo Diagnostics Driver wrapper did not open the device."));
            break;
        }

        const auto readMsr = reinterpret_cast<LDDServiceReadMsrMethod>(vtable[11]);
        temperatures = ReadLenovoDriverIntelTemperatures(wrapperService, readMsr);
        if (temperatures.empty()) {
            diagnostics = ResourceStringText(RES_STR("Lenovo Diagnostics Driver returned no valid CPU temperature."));
            break;
        }
    } while (false);

    if (destroyService != nullptr && wrapperService != nullptr) {
        destroyService(wrapperService);
    }
    if (serviceCreated) {
        StopServiceBestEffort(service.Get());
        DeleteService(service.Get());
    }

    if (temperatures.empty()) {
        snapshot.diagnostics = diagnostics;
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("driver_cpu_temperature_failed diagnostics=\"%s\""),
            snapshot.diagnostics.c_str());
        return snapshot;
    }

    const auto [minimumIt, maximumIt] = std::minmax_element(temperatures.begin(), temperatures.end());
    snapshot.success = true;
    snapshot.temperatures.push_back(BoardSensorReading{kLenovoCpuTemperatureName, *maximumIt});
    snapshot.diagnostics =
        FormatText(RES_STR("Lenovo Diagnostics Driver CPU temperature query completed. logical_processors=%zu "
                           "minimum_c=%.1f maximum_c=%.1f"),
            temperatures.size(),
            *minimumIt,
            *maximumIt);
    trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
        RES_STR("driver_cpu_temperature_done logical_processors=%zu minimum_c=%.1f maximum_c=%.1f"),
        temperatures.size(),
        *minimumIt,
        *maximumIt);
    return snapshot;
}

std::optional<std::wstring> ReadWmiStringProperty(IWbemClassObject* object, const wchar_t* propertyName) {
    VARIANT value;
    VariantInit(&value);
    const HRESULT hr = object->Get(propertyName, 0, &value, nullptr, nullptr);
    std::optional<std::wstring> result;
    if (SUCCEEDED(hr) && value.vt == VT_BSTR && value.bstrVal != nullptr) {
        result = std::wstring(value.bstrVal, SysStringLen(value.bstrVal));
    }
    VariantClear(&value);
    return result;
}

std::optional<std::uint32_t> ReadWmiUInt32Property(IWbemClassObject* object, const wchar_t* propertyName) {
    VARIANT value;
    VariantInit(&value);
    const HRESULT hr = object->Get(propertyName, 0, &value, nullptr, nullptr);
    if (FAILED(hr)) {
        VariantClear(&value);
        return std::nullopt;
    }

    VARIANT converted;
    VariantInit(&converted);
    const HRESULT convertHr = VariantChangeType(&converted, &value, 0, VT_UI4);
    VariantClear(&value);
    if (FAILED(convertHr)) {
        VariantClear(&converted);
        return std::nullopt;
    }

    const std::uint32_t result = converted.ulVal;
    VariantClear(&converted);
    return result;
}

std::optional<std::uint32_t> ExecuteLenovoGameZoneMethod(
    Trace& trace, IWbemServices* services, const std::wstring& objectPath, const wchar_t* methodName) {
    Bstr path(objectPath.c_str());
    Bstr method(methodName);
    if (!path.Valid() || !method.Valid()) {
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_method method=\"%s\" status=alloc_failed"),
            TextFromNullableWide(methodName).c_str());
        return std::nullopt;
    }

    ComObject<IWbemClassObject> output;
    const HRESULT hr = services->ExecMethod(path.Get(), method.Get(), 0, nullptr, nullptr, output.Out(), nullptr);
    if (FAILED(hr)) {
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_method method=\"%s\" status=%s"),
            TextFromNullableWide(methodName).c_str(),
            FormatHresult(hr).c_str());
        return std::nullopt;
    }

    const std::optional<std::uint32_t> value = ReadWmiUInt32Property(output.Get(), kWmiDataProperty);
    if (!value.has_value()) {
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_method method=\"%s\" status=no_data"),
            TextFromNullableWide(methodName).c_str());
        return std::nullopt;
    }

    trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
        RES_STR("gamezone_wmi_method method=\"%s\" status=ok data=%lu"),
        TextFromNullableWide(methodName).c_str(),
        static_cast<unsigned long>(*value));
    return value;
}

void AddLenovoGameZoneFanReading(
    std::vector<BoardSensorReading>& fans, const char* title, std::optional<std::uint32_t> rpm) {
    if (rpm.has_value() && IsSaneRpm(static_cast<double>(*rpm))) {
        fans.push_back(BoardSensorReading{title, static_cast<double>(*rpm)});
    }
}

std::string FormatOptionalUInt32(std::optional<std::uint32_t> value) {
    return value.has_value() ? FormatText("%lu", static_cast<unsigned long>(*value)) : std::string("N/A");
}

void AddLenovoGameZoneFanReadings(std::vector<BoardSensorReading>& fans,
    std::optional<std::uint32_t> fanCount,
    std::optional<std::uint32_t> fan1,
    std::optional<std::uint32_t> fan2) {
    LenovoGameZoneFanValue values[] = {
        {fan1.value_or(0)},
        {fan2.value_or(0)},
    };
    const int validCount =
        static_cast<int>(std::count_if(std::begin(values), std::end(values), [](const LenovoGameZoneFanValue& value) {
            return IsSaneRpm(static_cast<double>(value.rpm));
        }));
    if (validCount == 1 && (!fanCount.has_value() || *fanCount <= 1)) {
        const LenovoGameZoneFanValue& value = IsSaneRpm(static_cast<double>(values[0].rpm)) ? values[0] : values[1];
        fans.push_back(BoardSensorReading{"Fan", static_cast<double>(value.rpm)});
        return;
    }

    AddLenovoGameZoneFanReading(fans, "CPU Fan", fan1);
    AddLenovoGameZoneFanReading(fans, "GPU Fan", fan2);
}

LenovoSensorSnapshot CaptureLenovoGameZoneWmiFans(Trace& trace) {
    LenovoSensorSnapshot snapshot;
    const ComApartment com;
    if (!com.Ready()) {
        snapshot.diagnostics = FormatText(RES_STR("Lenovo GameZone WMI fan query COM initialization failed: %s"),
            FormatHresult(com.Status()).c_str());
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_failed stage=co_initialize status=%s"),
            FormatHresult(com.Status()).c_str());
        return snapshot;
    }

    const HRESULT securityHr = CoInitializeSecurity(nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr);
    if (FAILED(securityHr) && securityHr != RPC_E_TOO_LATE) {
        snapshot.diagnostics = FormatText(
            RES_STR("Lenovo GameZone WMI fan query COM security failed: %s"), FormatHresult(securityHr).c_str());
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_failed stage=co_initialize_security status=%s"),
            FormatHresult(securityHr).c_str());
        return snapshot;
    }

    ComObject<IWbemLocator> locator;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(locator.Out()));
    if (FAILED(hr)) {
        snapshot.diagnostics =
            FormatText(RES_STR("Lenovo GameZone WMI locator creation failed: %s"), FormatHresult(hr).c_str());
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_failed stage=create_locator status=%s"),
            FormatHresult(hr).c_str());
        return snapshot;
    }

    Bstr namespacePath(kLenovoGameZoneNamespace);
    if (!namespacePath.Valid()) {
        snapshot.diagnostics = ResourceStringText(RES_STR("Lenovo GameZone WMI namespace allocation failed."));
        trace.Write(TracePrefix::LenovoDiagnosticsDriver, RES_STR("gamezone_wmi_failed stage=namespace_alloc"));
        return snapshot;
    }

    ComObject<IWbemServices> services;
    hr = locator.Get()->ConnectServer(
        namespacePath.Get(), nullptr, nullptr, nullptr, 0, nullptr, nullptr, services.Out());
    if (FAILED(hr)) {
        snapshot.diagnostics =
            FormatText(RES_STR("Lenovo GameZone WMI connection failed: %s"), FormatHresult(hr).c_str());
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_failed stage=connect status=%s"),
            FormatHresult(hr).c_str());
        return snapshot;
    }

    hr = CoSetProxyBlanket(services.Get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);
    if (FAILED(hr)) {
        snapshot.diagnostics =
            FormatText(RES_STR("Lenovo GameZone WMI proxy security failed: %s"), FormatHresult(hr).c_str());
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_failed stage=proxy_blanket status=%s"),
            FormatHresult(hr).c_str());
        return snapshot;
    }

    Bstr className(kLenovoGameZoneClass);
    if (!className.Valid()) {
        snapshot.diagnostics = ResourceStringText(RES_STR("Lenovo GameZone WMI class allocation failed."));
        trace.Write(TracePrefix::LenovoDiagnosticsDriver, RES_STR("gamezone_wmi_failed stage=class_alloc"));
        return snapshot;
    }

    ComObject<IEnumWbemClassObject> enumerator;
    hr = services.Get()->CreateInstanceEnum(
        className.Get(), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, enumerator.Out());
    if (FAILED(hr)) {
        snapshot.diagnostics =
            FormatText(RES_STR("Lenovo GameZone WMI instance enumeration failed: %s"), FormatHresult(hr).c_str());
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_failed stage=enumerate status=%s"),
            FormatHresult(hr).c_str());
        return snapshot;
    }

    int instanceCount = 0;
    std::optional<std::uint32_t> lastFanCount;
    std::optional<std::uint32_t> lastFan1;
    std::optional<std::uint32_t> lastFan2;
    for (;;) {
        ComObject<IWbemClassObject> instance;
        ULONG returned = 0;
        hr = enumerator.Get()->Next(kWmiQueryTimeoutMs, 1, instance.Out(), &returned);
        if (hr == WBEM_S_FALSE || returned == 0) {
            break;
        }
        if (FAILED(hr)) {
            snapshot.diagnostics =
                FormatText(RES_STR("Lenovo GameZone WMI instance read failed: %s"), FormatHresult(hr).c_str());
            trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
                RES_STR("gamezone_wmi_failed stage=next status=%s"),
                FormatHresult(hr).c_str());
            return snapshot;
        }

        const std::optional<std::wstring> objectPath = ReadWmiStringProperty(instance.Get(), kWmiRelPathProperty);
        if (!objectPath.has_value() || objectPath->empty()) {
            trace.Write(TracePrefix::LenovoDiagnosticsDriver, RES_STR("gamezone_wmi_instance status=no_path"));
            continue;
        }

        ++instanceCount;
        trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("gamezone_wmi_instance path=\"%s\""),
            TextFromWide(*objectPath).c_str());

        const std::optional<std::uint32_t> fanCount =
            ExecuteLenovoGameZoneMethod(trace, services.Get(), *objectPath, kGetFanCountMethod);
        const std::optional<std::uint32_t> fan1 =
            ExecuteLenovoGameZoneMethod(trace, services.Get(), *objectPath, kGetFan1SpeedMethod);
        const std::optional<std::uint32_t> fan2 =
            ExecuteLenovoGameZoneMethod(trace, services.Get(), *objectPath, kGetFan2SpeedMethod);
        lastFanCount = fanCount;
        lastFan1 = fan1;
        lastFan2 = fan2;
        AddLenovoGameZoneFanReadings(snapshot.fans, fanCount, fan1, fan2);
    }

    snapshot.success = true;
    snapshot.diagnostics =
        FormatText(RES_STR("Lenovo GameZone WMI fan query completed. instance_count=%d fan_count=%zu "
                           "fan_count_raw=%s fan1_raw=%s fan2_raw=%s"),
            instanceCount,
            snapshot.fans.size(),
            FormatOptionalUInt32(lastFanCount).c_str(),
            FormatOptionalUInt32(lastFan1).c_str(),
            FormatOptionalUInt32(lastFan2).c_str());
    trace.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
        RES_STR("gamezone_wmi_done instance_count=%d fan_count=%zu fan_names=\"%s\""),
        instanceCount,
        snapshot.fans.size(),
        JoinNames(ExtractBoardSensorNames(snapshot.fans)).c_str());
    return snapshot;
}

void AppendDiagnosticsSuffix(std::string& diagnostics, const char* label, const std::string& suffix) {
    if (suffix.empty()) {
        return;
    }
    if (diagnostics.empty()) {
        diagnostics = suffix;
        return;
    }
    AppendFormat(diagnostics, RES_STR(" %s=\"%s\""), label, suffix.c_str());
}

void AppendFanReadings(std::vector<BoardSensorReading>& target, const std::vector<BoardSensorReading>& source) {
    for (const BoardSensorReading& reading : source) {
        if (reading.value.has_value() && IsSaneRpm(*reading.value)) {
            target.push_back(reading);
        }
    }
}

void AppendLenovoGameZoneWmiFans(Trace& trace, LenovoSensorSnapshot& snapshot) {
    LenovoSensorSnapshot gameZone = CaptureLenovoGameZoneWmiFans(trace);
    AppendDiagnosticsSuffix(snapshot.diagnostics, "gamezone_fans", gameZone.diagnostics);
    if (!gameZone.success) {
        return;
    }
    AppendFanReadings(snapshot.fans, gameZone.fans);
    snapshot.success = snapshot.success || HasAvailableFanReading(snapshot);
}

std::vector<NamedScalarMetric> CreateRawMetrics(
    const std::vector<BoardSensorReading>& readings, ScalarMetricUnit unit) {
    std::vector<NamedScalarMetric> metrics;
    metrics.reserve(readings.size());
    for (const BoardSensorReading& reading : readings) {
        metrics.push_back(NamedScalarMetric{reading.title, ScalarMetric{reading.value, unit}});
    }
    return metrics;
}

void MarkMissingMetricsPermissionRequired(std::vector<NamedScalarMetric>& metrics) {
    for (NamedScalarMetric& metric : metrics) {
        if (!metric.metric.value.has_value()) {
            metric.metric.issue = ScalarMetricIssue::PermissionRequired;
        }
    }
}

std::optional<BoardVendorTelemetrySample> QueryServiceBoardSample(std::string& diagnostics) {
    diagnostics.clear();
    if (!WaitNamedPipeA(kFpsServicePipeName, kPipeConnectTimeoutMs)) {
        diagnostics =
            FormatText(RES_STR("CashDash service pipe is unavailable: %s"), FormatWin32Error(GetLastError()).c_str());
        return std::nullopt;
    }

    Handle pipe(CreateFileA(
        kFpsServicePipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (pipe.Get() == INVALID_HANDLE_VALUE) {
        diagnostics = FormatText(
            RES_STR("Failed to connect to CashDash service pipe: %s"), FormatWin32Error(GetLastError()).c_str());
        return std::nullopt;
    }

    const std::vector<char> request = BuildBoardSensorsServiceRequest();
    DWORD written = 0;
    if (!WriteFile(pipe.Get(), request.data(), static_cast<DWORD>(request.size()), &written, nullptr) ||
        written != request.size()) {
        diagnostics = FormatText(
            RES_STR("Failed to write board sensor service request: %s"), FormatWin32Error(GetLastError()).c_str());
        return std::nullopt;
    }

    std::vector<char> response;
    for (;;) {
        char buffer[kPipeReadChunkBytes]{};
        DWORD read = 0;
        if (!ReadFile(pipe.Get(), buffer, static_cast<DWORD>(std::size(buffer)), &read, nullptr)) {
            const DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                break;
            }
            diagnostics = FormatText(
                RES_STR("Failed to read board sensor service response: %s"), FormatWin32Error(error).c_str());
            return std::nullopt;
        }
        if (read == 0) {
            break;
        }
        if (response.size() + read > kMaximumPipeResponseBytes) {
            diagnostics = ResourceStringText(RES_STR("Board sensor service response is too large."));
            return std::nullopt;
        }
        response.insert(response.end(), buffer, buffer + read);
    }

    return ParseBoardSensorsServiceResponse(response.data(), response.size(), diagnostics);
}

LenovoSensorSnapshot SnapshotFromServiceSample(const BoardVendorTelemetrySample& sample) {
    LenovoSensorSnapshot snapshot;
    snapshot.success = sample.available;
    snapshot.diagnostics = sample.diagnostics.empty()
                               ? ResourceStringText(RES_STR("Lenovo Diagnostics Driver service sample completed."))
                               : sample.diagnostics;
    snapshot.driverLibrary = sample.driverLibrary.empty() ? kLenovoDirectDriverLibrary : sample.driverLibrary;
    for (const NamedScalarMetric& metric : sample.fans) {
        snapshot.fans.push_back(BoardSensorReading{metric.name, metric.metric.value});
    }
    for (const NamedScalarMetric& metric : sample.temperatures) {
        snapshot.temperatures.push_back(BoardSensorReading{metric.name, metric.metric.value});
    }
    return snapshot;
}

BoardVendorTelemetrySample CreateRawLenovoSampleFromSnapshot(
    const BoardVendorInfo& info, const LenovoSensorSnapshot& snapshot) {
    BoardVendorTelemetrySample sample;
    sample.providerName = kLenovoProviderName;
    sample.boardManufacturer = info.manufacturer;
    sample.boardProduct = info.product;
    sample.driverLibrary = snapshot.driverLibrary.empty() ? kLenovoDirectDriverLibrary : snapshot.driverLibrary;
    sample.diagnostics = snapshot.diagnostics;
    sample.availableFanNames = ExtractBoardSensorNames(snapshot.fans);
    sample.availableTemperatureNames = ExtractBoardSensorNames(snapshot.temperatures);
    sample.fans = CreateRawMetrics(snapshot.fans, ScalarMetricUnit::Rpm);
    sample.temperatures = CreateRawMetrics(snapshot.temperatures, ScalarMetricUnit::Celsius);
    sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
    return sample;
}

class LenovoDiagnosticsDriverBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    LenovoDiagnosticsDriverBoardTelemetryProvider(Trace& trace, BoardVendorInfo info)
        : trace_(trace), info_(std::move(info)) {}

    bool Initialize(const BoardTelemetrySettings& settings) override {
        settings_ = settings;
        wantsDriverTemperature_ = !settings_.requestedTemperatureNames.empty();
        wantsGameZoneFans_ = !settings_.requestedFanNames.empty();
        trace_.Write(TracePrefix::LenovoDiagnosticsDriver, RES_STR("initialize_begin"));

        boardManufacturer_ = info_.manufacturer;
        boardProduct_ = info_.product;
        trace_.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("board manufacturer=\"%s\" product=\"%s\""),
            boardManufacturer_.c_str(),
            boardProduct_.c_str());

        if (SelectBoardVendor(info_) != BoardVendor::Lenovo) {
            diagnostics_ = ResourceStringText(RES_STR("Baseboard manufacturer is not Lenovo."));
            return false;
        }

        diagnosticsDriverDirectory_ = FindInstalledLenovoDiagnosticsDriverDirectory();
        if (!diagnosticsDriverDirectory_.has_value()) {
            diagnostics_ = ResourceStringText(RES_STR("Lenovo Diagnostics Driver addin directory was not found."));
            return false;
        }

        driverLibrary_ = (*diagnosticsDriverDirectory_ / kLenovoDiagnosticsDriverServiceDll).string();
        diagnostics_ = ResourceStringText(RES_STR("Lenovo Diagnostics Driver provider ready."));
        temperatureMetricTemplate_ =
            CreateRequestedBoardMetrics(settings_.requestedTemperatureNames, ScalarMetricUnit::Celsius);
        fanMetricTemplate_ = CreateRequestedBoardMetrics(settings_.requestedFanNames, ScalarMetricUnit::Rpm);
        availableTemperatureNames_ =
            wantsDriverTemperature_ ? std::vector<std::string>{kLenovoCpuTemperatureName} : std::vector<std::string>{};
        requestedTemperatureIndexBySourceName_.clear();
        requestedFanIndexBySourceName_.clear();
        for (size_t i = 0; i < temperatureMetricTemplate_.size(); ++i) {
            AppendRequestedBoardMetricIndex(requestedTemperatureIndexBySourceName_,
                ResolveTemperatureSensorName(temperatureMetricTemplate_[i].name),
                i);
        }
        for (size_t i = 0; i < fanMetricTemplate_.size(); ++i) {
            AppendRequestedBoardMetricIndex(
                requestedFanIndexBySourceName_, ResolveFanSensorName(fanMetricTemplate_[i].name), i);
        }

        requestedDiagnosticsSuffix_.clear();
        if (!settings_.requestedTemperatureNames.empty()) {
            AppendFormat(requestedDiagnosticsSuffix_,
                RES_STR(" requested_temps=%s"),
                JoinNames(settings_.requestedTemperatureNames).c_str());
        }
        if (!settings_.requestedFanNames.empty()) {
            AppendFormat(requestedDiagnosticsSuffix_,
                RES_STR(" requested_fans=%s"),
                JoinNames(settings_.requestedFanNames).c_str());
        }
        initialized_ = true;
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        BoardVendorTelemetrySample sample = CreateBaseSample();
        if (!initialized_ || !diagnosticsDriverDirectory_.has_value()) {
            return sample;
        }
        if (!processElevated_ && wantsDriverTemperature_) {
            return SampleThroughService(sample);
        }
        return SampleDirectly(sample);
    }

private:
    BoardVendorTelemetrySample SampleThroughService(BoardVendorTelemetrySample sample) {
        std::string serviceDiagnostics;
        if (!serviceUsable_) {
            ++serviceRetrySample_;
            if (serviceRetrySample_ < kSensorRetrySampleInterval) {
                serviceDiagnostics =
                    ResourceStringText(RES_STR("CashDash service Lenovo direct-driver path is waiting for retry."));
                ApplyDriverPermissionRequiredSample(sample, serviceDiagnostics);
                return sample;
            }
            serviceRetrySample_ = 0;
        }

        trace_.Write(TracePrefix::LenovoDiagnosticsDriver, RES_STR("service_sample_refresh_started"));
        std::optional<BoardVendorTelemetrySample> serviceSample = QueryServiceBoardSample(serviceDiagnostics);
        if (!serviceSample.has_value()) {
            serviceUsable_ = false;
            serviceRetrySample_ = 0;
            trace_.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
                RES_STR("service_sample_failed diagnostics=\"%s\""),
                serviceDiagnostics.c_str());
            ApplyDriverPermissionRequiredSample(sample, serviceDiagnostics);
            return sample;
        }

        serviceUsable_ = true;
        serviceRetrySample_ = kSensorRetrySampleInterval;
        LenovoSensorSnapshot snapshot = SnapshotFromServiceSample(*serviceSample);
        trace_.WriteFmt(TracePrefix::LenovoDiagnosticsDriver,
            RES_STR("service_sample_done available=%d diagnostics=\"%s\""),
            snapshot.success ? 1 : 0,
            snapshot.diagnostics.c_str());
        if (snapshot.success) {
            ApplySnapshotToSample(snapshot, sample);
            return sample;
        }

        ApplyFanOnlySample(sample);
        diagnostics_ = snapshot.diagnostics.empty() ? serviceDiagnostics : snapshot.diagnostics;
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
        return sample;
    }

    BoardVendorTelemetrySample SampleDirectly(BoardVendorTelemetrySample sample) {
        std::string directDiagnostics;
        LenovoSensorSnapshot snapshot;
        if (wantsDriverTemperature_) {
            trace_.Write(TracePrefix::LenovoDiagnosticsDriver, RES_STR("direct_snapshot_refresh_started"));
            snapshot = CaptureLenovoDriverCpuTemperatureSensors(trace_, *diagnosticsDriverDirectory_);
            directDiagnostics = snapshot.diagnostics;
        } else {
            directDiagnostics =
                ResourceStringText(RES_STR("No Lenovo Diagnostics Driver temperature values were requested."));
        }
        AppendGameZoneFans(snapshot);

        if (snapshot.success) {
            ApplySnapshotToSample(snapshot, sample);
            return sample;
        }

        diagnostics_ =
            FormatText(RES_STR("Lenovo Diagnostics Driver unavailable. direct=\"%s\""), directDiagnostics.c_str());
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
        return sample;
    }

    BoardVendorTelemetrySample CreateBaseSample() const {
        BoardVendorTelemetrySample sample;
        sample.providerName = kLenovoProviderName;
        sample.requestedFanNames = settings_.requestedFanNames;
        sample.requestedTemperatureNames = settings_.requestedTemperatureNames;
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;
        sample.boardManufacturer = boardManufacturer_;
        sample.boardProduct = boardProduct_;
        sample.driverLibrary = driverLibrary_;
        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
        return sample;
    }

    void ApplySnapshotToSample(const LenovoSensorSnapshot& snapshot, BoardVendorTelemetrySample& sample) {
        diagnostics_ = snapshot.diagnostics;
        if (!snapshot.driverLibrary.empty()) {
            driverLibrary_ = snapshot.driverLibrary;
            sample.driverLibrary = driverLibrary_;
        }
        availableFanNames_ = ExtractBoardSensorNames(snapshot.fans);
        availableTemperatureNames_ = ExtractBoardSensorNames(snapshot.temperatures);
        if (availableTemperatureNames_.empty() && wantsDriverTemperature_) {
            availableTemperatureNames_ = {kLenovoCpuTemperatureName};
        }
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;

        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        ResetBoardMetricValues(sample.temperatures);
        ResetBoardMetricValues(sample.fans);
        ApplyBoardSensorReadingsToMetrics(
            snapshot.temperatures, requestedTemperatureIndexBySourceName_, sample.temperatures);
        ApplyBoardSensorReadingsToMetrics(snapshot.fans, requestedFanIndexBySourceName_, sample.fans);
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
    }

    void ApplyFanOnlySample(BoardVendorTelemetrySample& sample) {
        std::string gameZoneDiagnostics;
        LenovoSensorSnapshot gameZoneSnapshot = CaptureGameZoneFanSnapshot(gameZoneDiagnostics);
        if (!gameZoneSnapshot.success || !HasAvailableFanReading(gameZoneSnapshot)) {
            AppendDiagnosticsSuffix(diagnostics_, "gamezone_fans", gameZoneDiagnostics);
            return;
        }

        availableFanNames_ = ExtractBoardSensorNames(gameZoneSnapshot.fans);
        sample.availableFanNames = availableFanNames_;
        sample.fans = fanMetricTemplate_;
        ResetBoardMetricValues(sample.fans);
        ApplyBoardSensorReadingsToMetrics(gameZoneSnapshot.fans, requestedFanIndexBySourceName_, sample.fans);
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        AppendDiagnosticsSuffix(diagnostics_, "gamezone_fans", gameZoneDiagnostics);
    }

    void ApplyDriverPermissionRequiredSample(
        BoardVendorTelemetrySample& sample, const std::string& serviceDiagnostics) {
        diagnostics_ = FormatText(
            RES_STR(
                "Lenovo Diagnostics Driver requires administrator privileges without CashDashService. service=\"%s\""),
            serviceDiagnostics.c_str());

        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        ResetBoardMetricValues(sample.temperatures);
        ResetBoardMetricValues(sample.fans);
        MarkMissingMetricsPermissionRequired(sample.temperatures);
        ApplyFanOnlySample(sample);
        MarkMissingMetricsPermissionRequired(sample.fans);
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
    }

    std::string ResolveTemperatureSensorName(const std::string& logicalName) const {
        const std::string mapped = ResolveMappedBoardSensorName(settings_.temperatureSensorNames, logicalName);
        return EqualsInsensitive(mapped, "cpu") ? std::string(kLenovoCpuTemperatureName) : mapped;
    }

    std::string ResolveFanSensorName(const std::string& logicalName) const {
        const std::string mapped = ResolveMappedBoardSensorName(settings_.fanSensorNames, logicalName);
        if (EqualsInsensitive(mapped, "cpu")) {
            return "CPU Fan";
        }
        if (EqualsInsensitive(mapped, "gpu")) {
            return "GPU Fan";
        }
        return mapped;
    }

    LenovoSensorSnapshot CaptureGameZoneFanSnapshot(std::string& diagnostics) {
        diagnostics.clear();
        if (!wantsGameZoneFans_) {
            return {};
        }
        if (!gameZoneFanUsable_) {
            ++gameZoneFanRetrySample_;
            if (gameZoneFanRetrySample_ < kSensorRetrySampleInterval) {
                diagnostics = ResourceStringText(RES_STR("Lenovo GameZone WMI fan query is waiting for retry."));
                return {};
            }
            gameZoneFanRetrySample_ = 0;
        }

        LenovoSensorSnapshot snapshot = CaptureLenovoGameZoneWmiFans(trace_);
        diagnostics = snapshot.diagnostics;
        if (!snapshot.success) {
            gameZoneFanUsable_ = false;
            gameZoneFanRetrySample_ = 0;
            return {};
        }
        if (!HasAvailableFanReading(snapshot)) {
            gameZoneFanUsable_ = false;
            gameZoneFanRetrySample_ = 0;
            return snapshot;
        }

        gameZoneFanUsable_ = true;
        gameZoneFanRetrySample_ = kSensorRetrySampleInterval;
        return snapshot;
    }

    void AppendGameZoneFans(LenovoSensorSnapshot& snapshot) {
        if (!wantsGameZoneFans_ || HasAvailableFanReading(snapshot)) {
            return;
        }

        std::string diagnostics;
        LenovoSensorSnapshot gameZone = CaptureGameZoneFanSnapshot(diagnostics);
        AppendDiagnosticsSuffix(snapshot.diagnostics, "gamezone_fans", diagnostics);
        if (!gameZone.success) {
            return;
        }
        AppendFanReadings(snapshot.fans, gameZone.fans);
        snapshot.success = snapshot.success || HasAvailableFanReading(snapshot);
    }

    Trace& trace_;
    BoardVendorInfo info_;
    BoardTelemetrySettings settings_{};
    std::optional<FilePath> diagnosticsDriverDirectory_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string driverLibrary_;
    std::string diagnostics_ = ResourceStringText(RES_STR("Lenovo Diagnostics Driver provider not initialized."));
    std::string requestedDiagnosticsSuffix_;
    std::vector<std::string> availableFanNames_;
    std::vector<std::string> availableTemperatureNames_;
    std::vector<NamedScalarMetric> fanMetricTemplate_;
    std::vector<NamedScalarMetric> temperatureMetricTemplate_;
    BoardMetricIndexBySourceName requestedFanIndexBySourceName_;
    BoardMetricIndexBySourceName requestedTemperatureIndexBySourceName_;
    int serviceRetrySample_ = kSensorRetrySampleInterval;
    int gameZoneFanRetrySample_ = kSensorRetrySampleInterval;
    bool serviceUsable_ = true;
    bool gameZoneFanUsable_ = true;
    bool wantsDriverTemperature_ = false;
    bool wantsGameZoneFans_ = false;
    bool processElevated_ = IsCurrentProcessElevated();
    bool initialized_ = false;
};

}  // namespace

BoardVendorTelemetrySample CaptureLenovoBoardServiceSample(Trace& trace, BoardVendorInfo info) {
    if (SelectBoardVendor(info) != BoardVendor::Lenovo) {
        BoardVendorTelemetrySample sample;
        sample.providerName = "Unsupported";
        sample.boardManufacturer = info.manufacturer;
        sample.boardProduct = info.product;
        sample.diagnostics =
            ResourceStringText(RES_STR("No Lenovo Diagnostics Driver provider matches the baseboard manufacturer."));
        return sample;
    }

    const std::optional<FilePath> addinDirectory = FindInstalledLenovoDiagnosticsDriverDirectory();
    if (!addinDirectory.has_value()) {
        BoardVendorTelemetrySample sample;
        sample.providerName = kLenovoProviderName;
        sample.boardManufacturer = info.manufacturer;
        sample.boardProduct = info.product;
        sample.driverLibrary = kLenovoDirectDriverLibrary;
        sample.diagnostics = ResourceStringText(RES_STR("Lenovo Diagnostics Driver addin directory was not found."));
        return sample;
    }

    LenovoSensorSnapshot snapshot = CaptureLenovoDriverCpuTemperatureSensors(trace, *addinDirectory);
    AppendLenovoGameZoneWmiFans(trace, snapshot);
    return CreateRawLenovoSampleFromSnapshot(info, snapshot);
}

std::unique_ptr<BoardVendorTelemetryProvider> CreateLenovoBoardTelemetryProvider(Trace& trace, BoardVendorInfo info) {
    return std::make_unique<LenovoDiagnosticsDriverBoardTelemetryProvider>(trace, std::move(info));
}
