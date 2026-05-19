#include "telemetry/board/lenovo/board_lenovo_vantage.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <Wbemidl.h>
#include <wrl/client.h>

#include "telemetry/board/lenovo/board_lenovo_vantage_bridge.h"
#include "telemetry/fps_service_protocol.h"
#include "telemetry/impl/system_info_support.h"
#include "util/elevated_process.h"
#include "util/file_path.h"
#include "util/resource_strings.h"
#include "util/strings.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/utf8.h"
#include "util/win32_format.h"

namespace {

constexpr char kLenovoProviderName[] = "Lenovo";
constexpr char kLenovoDriverLibrary[] = "Lenovo Hardware Scan LdeApi";
constexpr DWORD kPipeConnectTimeoutMs = 100;
constexpr DWORD kPipeReadChunkBytes = 4096;
constexpr DWORD kMaximumPipeResponseBytes = 16 * 1024;
constexpr int kSensorRetrySampleInterval = 10;
constexpr std::chrono::seconds kDirectSnapshotRefreshInterval{5};
constexpr DWORD kWmiQueryTimeoutMs = 1500;
constexpr wchar_t kLenovoGameZoneNamespace[] = L"ROOT\\WMI";
constexpr wchar_t kLenovoGameZoneClass[] = L"LENOVO_GAMEZONE_DATA";

struct LenovoHardwareScanSnapshot {
    bool success = false;
    std::string diagnostics;
    std::vector<BoardSensorReading> fans;
    std::vector<BoardSensorReading> temperatures;
};

struct LenovoGameZoneFanValue {
    std::uint32_t rpm = 0;
};

struct LenovoServiceSnapshotState {
    std::mutex mutex;
    bool running = false;
    bool done = false;
    bool hasResponse = false;
    std::string diagnostics;
    LenovoHardwareScanSnapshot snapshot;
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

std::string Utf8FromNullableWide(const wchar_t* text) {
    return text != nullptr ? Utf8FromWide(text) : std::string();
}

std::string FormatHresult(HRESULT value) {
    std::string text;
    AppendHresult(text, value);
    return text;
}

bool IsSaneRpm(double value) {
    return value > 0.0 && value < 30000.0;
}

bool HasAvailableFanReading(const LenovoHardwareScanSnapshot& snapshot) {
    return std::any_of(snapshot.fans.begin(), snapshot.fans.end(), [](const BoardSensorReading& reading) {
        return reading.value.has_value() && IsSaneRpm(*reading.value);
    });
}

bool DirectoryExists(const FilePath& path) {
    const std::wstring wide = path.Wide();
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::optional<FilePath> ProgramDataDirectory() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetEnvironmentVariableW(L"ProgramData", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::nullopt;
    }
    return FilePath(std::wstring(buffer.data(), length));
}

std::vector<int> ParseVersionParts(const std::wstring& text) {
    std::vector<int> parts;
    int value = 0;
    bool hasDigits = false;
    for (const wchar_t ch : text) {
        if (ch >= L'0' && ch <= L'9') {
            value = value * 10 + static_cast<int>(ch - L'0');
            hasDigits = true;
            continue;
        }
        if (ch == L'.' && hasDigits) {
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

bool VersionGreater(const std::wstring& left, const std::wstring& right) {
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

bool IsHardwareScanDirectory(const FilePath& path) {
    return FileExists(path / "LenovoHardwareScanAddin.dll") && FileExists(path / "LdeApi.Client.dll") &&
           FileExists(path / "LdeApi.Server.exe") && FileExists(path / "Lenovo.Vantage.RpcClient.dll");
}

std::optional<FilePath> FindInstalledLenovoHardwareScanDirectory() {
    const std::optional<FilePath> programData = ProgramDataDirectory();
    if (!programData.has_value()) {
        return std::nullopt;
    }

    const FilePath addinRoot = *programData / "Lenovo" / "Vantage" / "Addins" / "LenovoHardwareScanAddin";
    if (!DirectoryExists(addinRoot)) {
        return std::nullopt;
    }

    const std::wstring pattern = (addinRoot / "*").Wide();
    WIN32_FIND_DATAW findData{};
    HANDLE search = FindFirstFileW(pattern.c_str(), &findData);
    if (search == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    std::optional<FilePath> bestPath;
    std::wstring bestVersion;
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            continue;
        }
        const std::wstring name = findData.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const FilePath candidate = addinRoot / FilePath(name);
        if (!IsHardwareScanDirectory(candidate)) {
            continue;
        }
        if (!bestPath.has_value() || VersionGreater(name, bestVersion)) {
            bestPath = candidate;
            bestVersion = name;
        }
    } while (FindNextFileW(search, &findData));

    FindClose(search);
    return bestPath;
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
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("gamezone_wmi_method method=\"%s\" status=alloc_failed"),
            Utf8FromNullableWide(methodName).c_str());
        return std::nullopt;
    }

    Microsoft::WRL::ComPtr<IWbemClassObject> output;
    const HRESULT hr =
        services->ExecMethod(path.Get(), method.Get(), 0, nullptr, nullptr, output.GetAddressOf(), nullptr);
    if (FAILED(hr)) {
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("gamezone_wmi_method method=\"%s\" status=%s"),
            Utf8FromNullableWide(methodName).c_str(),
            FormatHresult(hr).c_str());
        return std::nullopt;
    }

    const std::optional<std::uint32_t> value = ReadWmiUInt32Property(output.Get(), L"Data");
    if (!value.has_value()) {
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("gamezone_wmi_method method=\"%s\" status=no_data"),
            Utf8FromNullableWide(methodName).c_str());
        return std::nullopt;
    }

    trace.WriteFmt(TracePrefix::LenovoHardwareScan,
        RES_STR("gamezone_wmi_method method=\"%s\" status=ok data=%lu"),
        Utf8FromNullableWide(methodName).c_str(),
        static_cast<unsigned long>(*value));
    return value;
}

void AddLenovoGameZoneFanReading(
    std::vector<BoardSensorReading>& fans, const char* title, std::optional<std::uint32_t> rpm) {
    if (rpm.has_value() && IsSaneRpm(static_cast<double>(*rpm))) {
        fans.push_back(BoardSensorReading{title, static_cast<double>(*rpm)});
    }
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

LenovoHardwareScanSnapshot CaptureLenovoGameZoneWmiFans(Trace& trace) {
    LenovoHardwareScanSnapshot snapshot;
    const ComApartment com;
    if (!com.Ready()) {
        snapshot.diagnostics = FormatText(RES_STR("Lenovo GameZone WMI fan query COM initialization failed: %s"),
            FormatHresult(com.Status()).c_str());
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
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
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("gamezone_wmi_failed stage=co_initialize_security status=%s"),
            FormatHresult(securityHr).c_str());
        return snapshot;
    }

    Microsoft::WRL::ComPtr<IWbemLocator> locator;
    HRESULT hr =
        CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(locator.GetAddressOf()));
    if (FAILED(hr)) {
        snapshot.diagnostics =
            FormatText(RES_STR("Lenovo GameZone WMI locator creation failed: %s"), FormatHresult(hr).c_str());
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("gamezone_wmi_failed stage=create_locator status=%s"),
            FormatHresult(hr).c_str());
        return snapshot;
    }

    Bstr namespacePath(kLenovoGameZoneNamespace);
    if (!namespacePath.Valid()) {
        snapshot.diagnostics = ResourceStringText(RES_STR("Lenovo GameZone WMI namespace allocation failed."));
        trace.Write(TracePrefix::LenovoHardwareScan, RES_STR("gamezone_wmi_failed stage=namespace_alloc"));
        return snapshot;
    }

    Microsoft::WRL::ComPtr<IWbemServices> services;
    hr = locator->ConnectServer(
        namespacePath.Get(), nullptr, nullptr, nullptr, 0, nullptr, nullptr, services.GetAddressOf());
    if (FAILED(hr)) {
        snapshot.diagnostics =
            FormatText(RES_STR("Lenovo GameZone WMI connection failed: %s"), FormatHresult(hr).c_str());
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
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
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("gamezone_wmi_failed stage=proxy_blanket status=%s"),
            FormatHresult(hr).c_str());
        return snapshot;
    }

    Bstr className(kLenovoGameZoneClass);
    if (!className.Valid()) {
        snapshot.diagnostics = ResourceStringText(RES_STR("Lenovo GameZone WMI class allocation failed."));
        trace.Write(TracePrefix::LenovoHardwareScan, RES_STR("gamezone_wmi_failed stage=class_alloc"));
        return snapshot;
    }

    Microsoft::WRL::ComPtr<IEnumWbemClassObject> enumerator;
    hr = services->CreateInstanceEnum(
        className.Get(), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, enumerator.GetAddressOf());
    if (FAILED(hr)) {
        snapshot.diagnostics =
            FormatText(RES_STR("Lenovo GameZone WMI instance enumeration failed: %s"), FormatHresult(hr).c_str());
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("gamezone_wmi_failed stage=enumerate status=%s"),
            FormatHresult(hr).c_str());
        return snapshot;
    }

    int instanceCount = 0;
    for (;;) {
        Microsoft::WRL::ComPtr<IWbemClassObject> instance;
        ULONG returned = 0;
        hr = enumerator->Next(kWmiQueryTimeoutMs, 1, instance.GetAddressOf(), &returned);
        if (hr == WBEM_S_FALSE || returned == 0) {
            break;
        }
        if (FAILED(hr)) {
            snapshot.diagnostics =
                FormatText(RES_STR("Lenovo GameZone WMI instance read failed: %s"), FormatHresult(hr).c_str());
            trace.WriteFmt(TracePrefix::LenovoHardwareScan,
                RES_STR("gamezone_wmi_failed stage=next status=%s"),
                FormatHresult(hr).c_str());
            return snapshot;
        }

        const std::optional<std::wstring> objectPath = ReadWmiStringProperty(instance.Get(), L"__RELPATH");
        if (!objectPath.has_value() || objectPath->empty()) {
            trace.Write(TracePrefix::LenovoHardwareScan, RES_STR("gamezone_wmi_instance status=no_path"));
            continue;
        }

        ++instanceCount;
        trace.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("gamezone_wmi_instance path=\"%s\""),
            Utf8FromWide(*objectPath).c_str());

        const std::optional<std::uint32_t> fanCount =
            ExecuteLenovoGameZoneMethod(trace, services.Get(), *objectPath, L"GetFanCount");
        const std::optional<std::uint32_t> fan1 =
            ExecuteLenovoGameZoneMethod(trace, services.Get(), *objectPath, L"GetFan1Speed");
        const std::optional<std::uint32_t> fan2 =
            ExecuteLenovoGameZoneMethod(trace, services.Get(), *objectPath, L"GetFan2Speed");
        AddLenovoGameZoneFanReadings(snapshot.fans, fanCount, fan1, fan2);
    }

    snapshot.success = true;
    snapshot.diagnostics =
        FormatText(RES_STR("Lenovo GameZone WMI fan query completed. instance_count=%d fan_count=%zu"),
            instanceCount,
            snapshot.fans.size());
    trace.WriteFmt(TracePrefix::LenovoHardwareScan,
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

void AppendLenovoGameZoneWmiFans(Trace& trace, LenovoHardwareScanSnapshot& snapshot) {
    LenovoHardwareScanSnapshot gameZone = CaptureLenovoGameZoneWmiFans(trace);
    AppendDiagnosticsSuffix(snapshot.diagnostics, "gamezone_fans", gameZone.diagnostics);
    if (!gameZone.success) {
        return;
    }
    AppendFanReadings(snapshot.fans, gameZone.fans);
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

bool HasLogicalName(const std::vector<std::string>& names, const char* value) {
    return std::any_of(
        names.begin(), names.end(), [value](const std::string& name) { return EqualsInsensitive(name, value); });
}

bool HasUnknownTemperatureRequest(const std::vector<std::string>& names) {
    return std::any_of(names.begin(), names.end(), [](const std::string& name) {
        return !EqualsInsensitive(name, "cpu") && !EqualsInsensitive(name, "gpu") && !EqualsInsensitive(name, "disk") &&
               !EqualsInsensitive(name, "storage") && !EqualsInsensitive(name, "motherboard") &&
               !EqualsInsensitive(name, "system") && !EqualsInsensitive(name, "board") &&
               !EqualsInsensitive(name, "battery");
    });
}

LenovoHardwareScanCaptureOptions CaptureOptionsForSettings(const BoardTelemetrySettings& settings) {
    LenovoHardwareScanCaptureOptions options{};
    const std::vector<std::string>& temperatures = settings.requestedTemperatureNames;
    const bool unknownTemperature = HasUnknownTemperatureRequest(temperatures);
    options.includeCpuTemperature = HasLogicalName(temperatures, "cpu") || unknownTemperature;
    options.includeGpuTemperature = HasLogicalName(temperatures, "gpu") || unknownTemperature;
    options.includeStorageTemperature =
        HasLogicalName(temperatures, "disk") || HasLogicalName(temperatures, "storage") || unknownTemperature;
    options.includeMotherboardTemperature = HasLogicalName(temperatures, "motherboard") ||
                                            HasLogicalName(temperatures, "system") ||
                                            HasLogicalName(temperatures, "board") || unknownTemperature;
    options.includeBatteryTemperature = HasLogicalName(temperatures, "battery") || unknownTemperature;
    return options;
}

bool HasRequestedHardwareScanModule(const LenovoHardwareScanCaptureOptions& options) {
    return options.includeCpuTemperature || options.includeGpuTemperature || options.includeStorageTemperature ||
           options.includeMotherboardTemperature || options.includeBatteryTemperature;
}

std::optional<BoardVendorTelemetrySample> QueryServiceBoardSample(std::string& diagnostics) {
    diagnostics.clear();
    const std::wstring pipeName = WideFromUtf8(kFpsServicePipeName);
    if (!WaitNamedPipeW(pipeName.c_str(), kPipeConnectTimeoutMs)) {
        diagnostics =
            FormatText(RES_STR("CashDash service pipe is unavailable: %s"), FormatWin32Error(GetLastError()).c_str());
        return std::nullopt;
    }

    Handle pipe(CreateFileW(
        pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
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

LenovoHardwareScanSnapshot SnapshotFromServiceSample(const BoardVendorTelemetrySample& sample) {
    LenovoHardwareScanSnapshot snapshot;
    snapshot.success = sample.available;
    snapshot.diagnostics = sample.diagnostics.empty()
                               ? ResourceStringText(RES_STR("Lenovo Hardware Scan service sample completed."))
                               : sample.diagnostics;
    for (const NamedScalarMetric& metric : sample.fans) {
        snapshot.fans.push_back(BoardSensorReading{metric.name, metric.metric.value});
    }
    for (const NamedScalarMetric& metric : sample.temperatures) {
        snapshot.temperatures.push_back(BoardSensorReading{metric.name, metric.metric.value});
    }
    return snapshot;
}

void StartLenovoServiceSnapshot(std::shared_ptr<LenovoServiceSnapshotState> state) {
    {
        std::lock_guard lock(state->mutex);
        if (state->running) {
            return;
        }
        state->running = true;
        state->done = false;
        state->hasResponse = false;
        state->diagnostics.clear();
        state->snapshot = {};
    }

    std::thread([state = std::move(state)]() {
        std::string diagnostics;
        std::optional<BoardVendorTelemetrySample> serviceSample = QueryServiceBoardSample(diagnostics);
        LenovoHardwareScanSnapshot snapshot;
        const bool hasResponse = serviceSample.has_value();
        if (hasResponse) {
            snapshot = SnapshotFromServiceSample(*serviceSample);
            diagnostics = snapshot.diagnostics;
        }

        std::lock_guard lock(state->mutex);
        state->snapshot = std::move(snapshot);
        state->diagnostics = std::move(diagnostics);
        state->hasResponse = hasResponse;
        state->done = true;
        state->running = false;
    }).detach();
}

class LenovoHardwareScanCapture final : public LenovoHardwareScanCaptureSink {
public:
    explicit LenovoHardwareScanCapture(Trace& trace) : trace_(trace) {}

    void AddTemperatureReading(const wchar_t* title, double celsius) override {
        snapshot_.temperatures.push_back(BoardSensorReading{Utf8FromNullableWide(title), celsius});
    }

    void SetDiagnostics(const wchar_t* diagnostics) override {
        snapshot_.diagnostics = Utf8FromNullableWide(diagnostics);
    }

    void TraceAssemblyLoaded(const wchar_t* path) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string pathText = Utf8FromNullableWide(path);
            trace_.WriteFmt(TracePrefix::LenovoHardwareScan, RES_STR("assembly_loaded path=\"%s\""), pathText.c_str());
        }
    }

    void TraceClientStatus(const wchar_t* status) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string statusText = Utf8FromNullableWide(status);
            trace_.WriteFmt(
                TracePrefix::LenovoHardwareScan, RES_STR("client_status status=\"%s\""), statusText.c_str());
        }
    }

    void TraceExecutionResult(const wchar_t* result) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string resultText = Utf8FromNullableWide(result);
            trace_.WriteFmt(
                TracePrefix::LenovoHardwareScan, RES_STR("execution_result result=\"%s\""), resultText.c_str());
        }
    }

    void TraceInitializeException(const wchar_t* diagnostics) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string diagnosticsText = Utf8FromNullableWide(diagnostics);
            trace_.WriteFmt(
                TracePrefix::LenovoHardwareScan, RES_STR("initialize_exception %s"), diagnosticsText.c_str());
        }
    }

    void TraceModuleLoadResult(const wchar_t* result) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string resultText = Utf8FromNullableWide(result);
            trace_.WriteFmt(TracePrefix::LenovoHardwareScan, RES_STR("module_load_result %s"), resultText.c_str());
        }
    }

    void TraceSnapshotException(const wchar_t* diagnostics) override {
        if (trace_.Enabled(TracePrefix::LenovoHardwareScan)) {
            const std::string diagnosticsText = Utf8FromNullableWide(diagnostics);
            trace_.WriteFmt(TracePrefix::LenovoHardwareScan, RES_STR("snapshot_exception %s"), diagnosticsText.c_str());
        }
    }

    LenovoHardwareScanSnapshot FinishSuccess() {
        snapshot_.success = true;
        snapshot_.diagnostics = FormatText(
            RES_STR("Lenovo Hardware Scan temperature query completed. temp_count=%zu"), snapshot_.temperatures.size());
        const std::vector<std::string> temperatureNames = ExtractBoardSensorNames(snapshot_.temperatures);
        trace_.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("snapshot_done temp_count=%zu temp_names=\"%s\""),
            snapshot_.temperatures.size(),
            JoinNames(temperatureNames).c_str());
        return std::move(snapshot_);
    }

    LenovoHardwareScanSnapshot FinishFailure() {
        return std::move(snapshot_);
    }

private:
    Trace& trace_;
    LenovoHardwareScanSnapshot snapshot_;
};

LenovoHardwareScanSnapshot CaptureLenovoHardwareScanSensors(Trace& trace,
    LenovoHardwareScanRuntime& runtime,
    const FilePath& addinDirectory,
    const LenovoHardwareScanCaptureOptions& options) {
    LenovoHardwareScanCapture capture(trace);
    const std::wstring wideAddinDirectory = addinDirectory.Wide();
    const bool captured = runtime.Capture(wideAddinDirectory.c_str(), options, capture);
    return captured ? capture.FinishSuccess() : capture.FinishFailure();
}

BoardVendorTelemetrySample CreateRawLenovoSampleFromSnapshot(
    const BoardVendorInfo& info, const LenovoHardwareScanSnapshot& snapshot) {
    BoardVendorTelemetrySample sample;
    sample.providerName = kLenovoProviderName;
    sample.boardManufacturer = info.manufacturer;
    sample.boardProduct = info.product;
    sample.driverLibrary = kLenovoDriverLibrary;
    sample.diagnostics = snapshot.diagnostics;
    sample.availableFanNames = ExtractBoardSensorNames(snapshot.fans);
    sample.availableTemperatureNames = ExtractBoardSensorNames(snapshot.temperatures);
    sample.fans = CreateRawMetrics(snapshot.fans, ScalarMetricUnit::Rpm);
    sample.temperatures = CreateRawMetrics(snapshot.temperatures, ScalarMetricUnit::Celsius);
    sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
    return sample;
}

class LenovoHardwareScanBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    LenovoHardwareScanBoardTelemetryProvider(Trace& trace, BoardVendorInfo info)
        : trace_(trace), info_(std::move(info)) {}

    ~LenovoHardwareScanBoardTelemetryProvider() override {
        if (pendingDirectSnapshot_.valid()) {
            pendingDirectSnapshot_.wait();
        }
    }

    bool Initialize(const BoardTelemetrySettings& settings) override {
        if (pendingDirectSnapshot_.valid()) {
            if (pendingDirectSnapshot_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                cachedDirectSnapshot_ = pendingDirectSnapshot_.get();
                hasCachedDirectSnapshot_ = cachedDirectSnapshot_.success;
                trace_.WriteFmt(TracePrefix::LenovoHardwareScan,
                    RES_STR("direct_snapshot_ready_during_initialize available=%d"),
                    cachedDirectSnapshot_.success ? 1 : 0);
            } else {
                trace_.Write(TracePrefix::LenovoHardwareScan, RES_STR("direct_snapshot_pending_during_initialize"));
            }
        }

        settings_ = settings;
        captureOptions_ = CaptureOptionsForSettings(settings_);
        wantsGameZoneFans_ = !settings_.requestedFanNames.empty();
        trace_.Write(TracePrefix::LenovoHardwareScan, RES_STR("initialize_begin"));

        boardManufacturer_ = info_.manufacturer;
        boardProduct_ = info_.product;
        trace_.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("board manufacturer=\"%s\" product=\"%s\""),
            boardManufacturer_.c_str(),
            boardProduct_.c_str());

        if (SelectBoardVendor(info_) != BoardVendor::Lenovo) {
            diagnostics_ = ResourceStringText(RES_STR("Baseboard manufacturer is not Lenovo."));
            return false;
        }

        hardwareScanDirectory_ = FindInstalledLenovoHardwareScanDirectory();
        if (!hardwareScanDirectory_.has_value()) {
            diagnostics_ = ResourceStringText(RES_STR("Lenovo Hardware Scan addin directory was not found."));
            return false;
        }

        driverLibrary_ = (*hardwareScanDirectory_ / "LenovoHardwareScanAddin.dll").string();
        diagnostics_ = ResourceStringText(RES_STR("Lenovo Hardware Scan provider ready."));
        temperatureMetricTemplate_ =
            CreateRequestedBoardMetrics(settings_.requestedTemperatureNames, ScalarMetricUnit::Celsius);
        fanMetricTemplate_ = CreateRequestedBoardMetrics(settings_.requestedFanNames, ScalarMetricUnit::Rpm);
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
        if (!initialized_ || !hardwareScanDirectory_.has_value()) {
            return sample;
        }

        std::string serviceDiagnostics;
        CompletePendingServiceSnapshot(serviceDiagnostics);
        MaybeStartServiceSnapshot(serviceDiagnostics);
        const bool serviceSnapshotRunning = IsServiceSnapshotRunning();
        if (hasCachedServiceSnapshot_) {
            ApplySnapshotToSample(cachedServiceSnapshot_, sample);
            if (serviceSnapshotRunning) {
                sample.diagnostics = FormatText(RES_STR("%s refresh=running service=\"%s\""),
                    sample.diagnostics.c_str(),
                    serviceDiagnostics.c_str());
            }
            return sample;
        }
        if (ServicePermissionRequired()) {
            ApplyServicePermissionRequiredSample(sample, serviceDiagnostics, serviceSnapshotRunning);
            return sample;
        }

        std::string directDiagnostics;
        CompletePendingDirectSnapshot(directDiagnostics);
        if (serviceSnapshotRunning) {
            if (directDiagnostics.empty()) {
                directDiagnostics =
                    ResourceStringText(RES_STR("Direct Lenovo Hardware Scan refresh is waiting for service sample."));
            }
        } else {
            MaybeStartDirectSnapshot(directDiagnostics);
        }

        if (hasCachedDirectSnapshot_) {
            ApplySnapshotToSample(cachedDirectSnapshot_, sample);
            if (pendingDirectSnapshot_.valid()) {
                sample.diagnostics = FormatText(RES_STR("%s refresh=running service=\"%s\""),
                    sample.diagnostics.c_str(),
                    serviceDiagnostics.c_str());
            }
            return sample;
        }

        if (directDiagnostics.empty()) {
            directDiagnostics = ResourceStringText(RES_STR("Direct Lenovo Hardware Scan refresh is waiting."));
        }
        std::string gameZoneDiagnostics;
        LenovoHardwareScanSnapshot gameZoneSnapshot;
        if (serviceSnapshotRunning) {
            gameZoneDiagnostics =
                ResourceStringText(RES_STR("Lenovo GameZone WMI fan query is waiting for service sample."));
        } else {
            gameZoneSnapshot = CaptureGameZoneFanSnapshot(gameZoneDiagnostics);
        }
        if (gameZoneSnapshot.success && HasAvailableFanReading(gameZoneSnapshot)) {
            gameZoneSnapshot.diagnostics = FormatText(
                RES_STR("Lenovo GameZone WMI fan query active. service=\"%s\" direct=\"%s\" gamezone=\"%s\""),
                serviceDiagnostics.c_str(),
                directDiagnostics.c_str(),
                gameZoneSnapshot.diagnostics.c_str());
            ApplySnapshotToSample(gameZoneSnapshot, sample);
            return sample;
        }

        diagnostics_ = FormatText(RES_STR("Lenovo Hardware Scan unavailable. service=\"%s\" direct=\"%s\""),
            serviceDiagnostics.c_str(),
            directDiagnostics.c_str());
        AppendDiagnosticsSuffix(diagnostics_, "gamezone_fans", gameZoneDiagnostics);
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
        return sample;
    }

private:
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

    void ApplySnapshotToSample(const LenovoHardwareScanSnapshot& snapshot, BoardVendorTelemetrySample& sample) {
        diagnostics_ = snapshot.diagnostics;
        availableFanNames_ = ExtractBoardSensorNames(snapshot.fans);
        availableTemperatureNames_ = ExtractBoardSensorNames(snapshot.temperatures);
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

    bool ServicePermissionRequired() const {
        return !processElevated_ && !serviceUsable_;
    }

    void ApplyServicePermissionRequiredSample(
        BoardVendorTelemetrySample& sample, const std::string& serviceDiagnostics, bool serviceSnapshotRunning) {
        std::string gameZoneDiagnostics;
        LenovoHardwareScanSnapshot gameZoneSnapshot;
        if (serviceSnapshotRunning) {
            gameZoneDiagnostics =
                ResourceStringText(RES_STR("Lenovo GameZone WMI fan query is waiting for service sample."));
        } else {
            gameZoneSnapshot = CaptureGameZoneFanSnapshot(gameZoneDiagnostics);
        }

        diagnostics_ = FormatText(
            RES_STR("Lenovo Hardware Scan requires administrator privileges without CashDashService. service=\"%s\""),
            serviceDiagnostics.c_str());
        AppendDiagnosticsSuffix(diagnostics_, "gamezone_fans", gameZoneDiagnostics);

        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        ResetBoardMetricValues(sample.temperatures);
        ResetBoardMetricValues(sample.fans);
        MarkMissingMetricsPermissionRequired(sample.temperatures);

        if (gameZoneSnapshot.success && HasAvailableFanReading(gameZoneSnapshot)) {
            availableFanNames_ = ExtractBoardSensorNames(gameZoneSnapshot.fans);
            sample.availableFanNames = availableFanNames_;
            ApplyBoardSensorReadingsToMetrics(gameZoneSnapshot.fans, requestedFanIndexBySourceName_, sample.fans);
        }
        MarkMissingMetricsPermissionRequired(sample.fans);

        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
    }

    bool IsServiceSnapshotRunning() {
        std::lock_guard lock(serviceSnapshotState_->mutex);
        return serviceSnapshotState_->running;
    }

    void CompletePendingServiceSnapshot(std::string& diagnostics) {
        LenovoHardwareScanSnapshot snapshot;
        bool done = false;
        bool hasResponse = false;
        {
            std::lock_guard lock(serviceSnapshotState_->mutex);
            if (serviceSnapshotState_->running && diagnostics.empty()) {
                diagnostics = ResourceStringText(RES_STR("CashDash service Lenovo Hardware Scan refresh is running."));
            }
            if (!serviceSnapshotState_->done) {
                return;
            }

            snapshot = std::move(serviceSnapshotState_->snapshot);
            diagnostics = std::move(serviceSnapshotState_->diagnostics);
            hasResponse = serviceSnapshotState_->hasResponse;
            serviceSnapshotState_->done = false;
            serviceSnapshotState_->hasResponse = false;
            done = true;
        }

        if (!done) {
            return;
        }

        lastServiceSnapshotStart_ = std::chrono::steady_clock::now();
        if (!hasResponse) {
            serviceUsable_ = false;
            serviceRetrySample_ = 0;
            cachedServiceSnapshot_ = {};
            hasCachedServiceSnapshot_ = false;
            if (!processElevated_) {
                cachedDirectSnapshot_ = {};
                hasCachedDirectSnapshot_ = false;
            }
            trace_.WriteFmt(TracePrefix::LenovoHardwareScan,
                RES_STR("service_sample_failed diagnostics=\"%s\""),
                diagnostics.c_str());
            return;
        }

        serviceUsable_ = true;
        serviceRetrySample_ = kSensorRetrySampleInterval;
        trace_.WriteFmt(TracePrefix::LenovoHardwareScan,
            RES_STR("service_sample_done available=%d diagnostics=\"%s\""),
            snapshot.success ? 1 : 0,
            snapshot.diagnostics.c_str());
        if (snapshot.success) {
            AppendGameZoneFans(snapshot);
            diagnostics = snapshot.diagnostics;
            cachedServiceSnapshot_ = std::move(snapshot);
            hasCachedServiceSnapshot_ = true;
        }
    }

    void MaybeStartServiceSnapshot(std::string& diagnostics) {
        {
            std::lock_guard lock(serviceSnapshotState_->mutex);
            if (serviceSnapshotState_->running) {
                if (diagnostics.empty()) {
                    diagnostics =
                        ResourceStringText(RES_STR("CashDash service Lenovo Hardware Scan refresh is running."));
                }
                return;
            }
        }

        if (!serviceUsable_) {
            ++serviceRetrySample_;
            if (serviceRetrySample_ < kSensorRetrySampleInterval) {
                if (diagnostics.empty()) {
                    diagnostics =
                        ResourceStringText(RES_STR("CashDash service Lenovo Hardware Scan path is waiting for retry."));
                }
                return;
            }
            serviceRetrySample_ = 0;
        }

        const auto now = std::chrono::steady_clock::now();
        if (lastServiceSnapshotStart_.has_value() &&
            now - *lastServiceSnapshotStart_ < kDirectSnapshotRefreshInterval) {
            if (diagnostics.empty()) {
                diagnostics = ResourceStringText(RES_STR("CashDash service Lenovo Hardware Scan refresh is waiting."));
            }
            return;
        }

        lastServiceSnapshotStart_ = now;
        diagnostics = ResourceStringText(RES_STR("CashDash service Lenovo Hardware Scan refresh started."));
        trace_.Write(TracePrefix::LenovoHardwareScan, RES_STR("service_sample_refresh_started"));
        StartLenovoServiceSnapshot(serviceSnapshotState_);
    }

    void CompletePendingDirectSnapshot(std::string& diagnostics) {
        if (!pendingDirectSnapshot_.valid()) {
            return;
        }
        if (pendingDirectSnapshot_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            diagnostics = ResourceStringText(RES_STR("Direct Lenovo Hardware Scan refresh is running."));
            return;
        }

        LenovoHardwareScanSnapshot snapshot = pendingDirectSnapshot_.get();
        diagnostics = snapshot.diagnostics;
        if (snapshot.success) {
            AppendGameZoneFans(snapshot);
            diagnostics = snapshot.diagnostics;
            cachedDirectSnapshot_ = std::move(snapshot);
            hasCachedDirectSnapshot_ = true;
        } else if (!hasCachedDirectSnapshot_) {
            diagnostics_ = snapshot.diagnostics;
        }
    }

    void MaybeStartDirectSnapshot(std::string& diagnostics) {
        if (pendingDirectSnapshot_.valid() || !hardwareScanDirectory_.has_value()) {
            return;
        }
        if (!HasRequestedHardwareScanModule(captureOptions_)) {
            diagnostics = ResourceStringText(RES_STR("No Lenovo Hardware Scan temperature modules were requested."));
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (lastDirectSnapshotStart_.has_value() && now - *lastDirectSnapshotStart_ < kDirectSnapshotRefreshInterval) {
            if (diagnostics.empty()) {
                diagnostics = ResourceStringText(RES_STR("Direct Lenovo Hardware Scan refresh is waiting."));
            }
            return;
        }

        const FilePath addinDirectory = *hardwareScanDirectory_;
        const LenovoHardwareScanCaptureOptions options = captureOptions_;
        lastDirectSnapshotStart_ = now;
        diagnostics = ResourceStringText(RES_STR("Direct Lenovo Hardware Scan refresh started."));
        trace_.Write(TracePrefix::LenovoHardwareScan, RES_STR("direct_snapshot_refresh_started"));
        pendingDirectSnapshot_ = std::async(std::launch::async, [this, addinDirectory, options]() {
            return CaptureLenovoHardwareScanSensors(trace_, runtime_, addinDirectory, options);
        });
    }

    std::string ResolveTemperatureSensorName(const std::string& logicalName) const {
        return ResolveMappedBoardSensorName(settings_.temperatureSensorNames, logicalName);
    }

    std::string ResolveFanSensorName(const std::string& logicalName) const {
        return ResolveMappedBoardSensorName(settings_.fanSensorNames, logicalName);
    }

    LenovoHardwareScanSnapshot CaptureGameZoneFanSnapshot(std::string& diagnostics) {
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

        LenovoHardwareScanSnapshot snapshot = CaptureLenovoGameZoneWmiFans(trace_);
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

    void AppendGameZoneFans(LenovoHardwareScanSnapshot& snapshot) {
        if (!wantsGameZoneFans_ || HasAvailableFanReading(snapshot)) {
            return;
        }

        std::string diagnostics;
        LenovoHardwareScanSnapshot gameZone = CaptureGameZoneFanSnapshot(diagnostics);
        AppendDiagnosticsSuffix(snapshot.diagnostics, "gamezone_fans", diagnostics);
        if (!gameZone.success) {
            return;
        }
        AppendFanReadings(snapshot.fans, gameZone.fans);
    }

    Trace& trace_;
    BoardVendorInfo info_;
    BoardTelemetrySettings settings_{};
    LenovoHardwareScanRuntime runtime_;
    LenovoHardwareScanCaptureOptions captureOptions_{};
    std::optional<FilePath> hardwareScanDirectory_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string driverLibrary_;
    std::string diagnostics_ = ResourceStringText(RES_STR("Lenovo Hardware Scan provider not initialized."));
    std::string requestedDiagnosticsSuffix_;
    std::vector<std::string> availableFanNames_;
    std::vector<std::string> availableTemperatureNames_;
    std::vector<NamedScalarMetric> fanMetricTemplate_;
    std::vector<NamedScalarMetric> temperatureMetricTemplate_;
    BoardMetricIndexBySourceName requestedFanIndexBySourceName_;
    BoardMetricIndexBySourceName requestedTemperatureIndexBySourceName_;
    std::shared_ptr<LenovoServiceSnapshotState> serviceSnapshotState_ = std::make_shared<LenovoServiceSnapshotState>();
    LenovoHardwareScanSnapshot cachedServiceSnapshot_;
    LenovoHardwareScanSnapshot cachedDirectSnapshot_;
    std::future<LenovoHardwareScanSnapshot> pendingDirectSnapshot_;
    std::optional<std::chrono::steady_clock::time_point> lastServiceSnapshotStart_;
    std::optional<std::chrono::steady_clock::time_point> lastDirectSnapshotStart_;
    int serviceRetrySample_ = kSensorRetrySampleInterval;
    int gameZoneFanRetrySample_ = kSensorRetrySampleInterval;
    bool serviceUsable_ = true;
    bool gameZoneFanUsable_ = true;
    bool wantsGameZoneFans_ = false;
    bool hasCachedServiceSnapshot_ = false;
    bool hasCachedDirectSnapshot_ = false;
    bool processElevated_ = IsCurrentProcessElevated();
    bool initialized_ = false;
};

}  // namespace

BoardVendorTelemetrySample CaptureLenovoHardwareScanServiceSample(Trace& trace, BoardVendorInfo info) {
    if (SelectBoardVendor(info) != BoardVendor::Lenovo) {
        BoardVendorTelemetrySample sample;
        sample.providerName = "Unsupported";
        sample.boardManufacturer = info.manufacturer;
        sample.boardProduct = info.product;
        sample.diagnostics =
            ResourceStringText(RES_STR("No Lenovo Hardware Scan provider matches the baseboard manufacturer."));
        return sample;
    }

    const std::optional<FilePath> addinDirectory = FindInstalledLenovoHardwareScanDirectory();
    if (!addinDirectory.has_value()) {
        BoardVendorTelemetrySample sample;
        sample.providerName = kLenovoProviderName;
        sample.boardManufacturer = info.manufacturer;
        sample.boardProduct = info.product;
        sample.driverLibrary = kLenovoDriverLibrary;
        sample.diagnostics = ResourceStringText(RES_STR("Lenovo Hardware Scan addin directory was not found."));
        return sample;
    }

    LenovoHardwareScanRuntime runtime;
    LenovoHardwareScanCaptureOptions options;
    LenovoHardwareScanSnapshot snapshot = CaptureLenovoHardwareScanSensors(trace, runtime, *addinDirectory, options);
    AppendLenovoGameZoneWmiFans(trace, snapshot);
    return CreateRawLenovoSampleFromSnapshot(info, snapshot);
}

std::unique_ptr<BoardVendorTelemetryProvider> CreateLenovoBoardTelemetryProvider(Trace& trace, BoardVendorInfo info) {
    return std::make_unique<LenovoHardwareScanBoardTelemetryProvider>(trace, std::move(info));
}
