#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <wbemidl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "telemetry.h"

namespace {

struct SensorRecord {
    std::wstring sensorNamespace;
    std::wstring name;
    std::wstring identifier;
    std::wstring parent;
    std::wstring sensorType;
    double value = 0.0;
};

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring EscapeWql(const std::wstring& value) {
    std::wstring escaped;
    for (wchar_t ch : value) {
        if (ch == L'\\' || ch == L'\'') {
            escaped.push_back(L'\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

class WmiSession {
public:
    bool Initialize();
    ~WmiSession();

    std::optional<std::wstring> QueryFirstString(
        const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property);
    std::optional<double> QueryFirstDouble(
        const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property);
    std::vector<SensorRecord> EnumerateSensors(const std::wstring& namespaceName);

private:
    bool QueryFirstVariant(
        const std::wstring& namespaceName, const std::wstring& query,
        const std::wstring& property, VARIANT& value);
    IWbemServices* GetServices(const std::wstring& namespaceName);

    bool initialized_ = false;
    bool comInitialized_ = false;
    IWbemLocator* locator_ = nullptr;
    std::map<std::wstring, IWbemServices*> services_;
};

std::optional<std::wstring> VariantToString(const VARIANT& value) {
    if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
        return std::wstring(value.bstrVal);
    }
    return std::nullopt;
}

std::optional<double> VariantToDouble(const VARIANT& value) {
    switch (value.vt) {
    case VT_R4:
        return value.fltVal;
    case VT_R8:
        return value.dblVal;
    case VT_I4:
    case VT_INT:
        return static_cast<double>(value.intVal);
    case VT_UI4:
        return static_cast<double>(value.uintVal);
    case VT_I8:
        return static_cast<double>(value.llVal);
    case VT_UI8:
        return static_cast<double>(value.ullVal);
    case VT_BSTR:
        if (value.bstrVal != nullptr) {
            try {
                return std::stod(value.bstrVal);
            } catch (...) {
                return std::nullopt;
            }
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

typedef PDH_STATUS(WINAPI* PdhAddEnglishCounterWFn)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);

PDH_STATUS AddCounterCompat(PDH_HQUERY query, const std::wstring& path, PDH_HCOUNTER* counter) {
    static PdhAddEnglishCounterWFn addEnglish = reinterpret_cast<PdhAddEnglishCounterWFn>(
        GetProcAddress(GetModuleHandleW(L"pdh.dll"), "PdhAddEnglishCounterW"));
    if (addEnglish != nullptr) {
        return addEnglish(query, path.c_str(), 0, counter);
    }
    return PdhAddCounterW(query, path.c_str(), 0, counter);
}

bool ContainsInsensitive(const std::wstring& value, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(value).find(ToLower(needle)) != std::wstring::npos;
}

bool ContainsAnyInsensitive(const std::wstring& value, const std::vector<std::wstring>& needles) {
    for (const auto& needle : needles) {
        if (ContainsInsensitive(value, needle)) {
            return true;
        }
    }
    return false;
}

std::wstring CombinedSensorText(const SensorRecord& sensor) {
    return ToLower(sensor.name + L" " + sensor.identifier + L" " + sensor.parent);
}

std::vector<std::wstring> DefaultSensorNamespaces() {
    return {L"root\\LibreHardwareMonitor", L"root\\OpenHardwareMonitor"};
}

std::vector<std::wstring> GatherSensorNamespaces(const AppConfig& config) {
    std::vector<std::wstring> namespaces = DefaultSensorNamespaces();
    for (const auto& pair : config.sensors) {
        for (const auto& ns : pair.second.namespaces) {
            if (ns.empty()) {
                continue;
            }
            const auto found = std::find(namespaces.begin(), namespaces.end(), ns);
            if (found == namespaces.end()) {
                namespaces.push_back(ns);
            }
        }
    }
    return namespaces;
}

std::optional<double> FindBestAutoSensorValue(
    const std::vector<SensorRecord>& sensors,
    const std::wstring& requiredType,
    const std::vector<std::wstring>& mustContain,
    const std::vector<std::wstring>& preferred,
    const std::vector<std::wstring>& discouraged,
    const std::vector<std::wstring>& forbidden) {
    const std::wstring normalizedType = ToLower(requiredType);
    double bestScore = -std::numeric_limits<double>::infinity();
    std::optional<double> bestValue;

    for (const auto& sensor : sensors) {
        if (ToLower(sensor.sensorType) != normalizedType) {
            continue;
        }

        const std::wstring text = CombinedSensorText(sensor);
        if (!mustContain.empty() && !ContainsAnyInsensitive(text, mustContain)) {
            continue;
        }
        if (ContainsAnyInsensitive(text, forbidden)) {
            continue;
        }

        double score = 0.0;
        for (const auto& token : preferred) {
            if (ContainsInsensitive(text, token)) {
                score += 10.0;
            }
        }
        for (const auto& token : mustContain) {
            if (ContainsInsensitive(text, token)) {
                score += 4.0;
            }
        }
        for (const auto& token : discouraged) {
            if (ContainsInsensitive(text, token)) {
                score -= 8.0;
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestValue = sensor.value;
        }
    }

    return bestValue;
}

}  // namespace

struct TelemetryCollector::Impl {
    void UpdateCpu();
    void UpdateGpu();
    void UpdateMemory();
    void UpdateSensors();
    void EnumerateDrives();
    void RefreshDriveUsage();
    void UpdateNetworkState(bool initializeOnly);
    std::optional<double> QuerySensor(const std::wstring& key);
    std::optional<double> QueryAutoSensor(const std::wstring& key, const std::vector<SensorRecord>& sensors);
    std::vector<SensorRecord> LoadSensors();
    double SumCounterArray(PDH_HCOUNTER counter, bool require3d);
    std::wstring FindAdapterIp(ULONG interfaceIndex);
    static void PushHistory(std::vector<double>& history, double value);

    AppConfig config_;
    SystemSnapshot snapshot_;
    WmiSession wmi_;

    PDH_HQUERY cpuQuery_ = nullptr;
    PDH_HCOUNTER cpuLoadCounter_ = nullptr;
    PDH_HCOUNTER cpuFrequencyCounter_ = nullptr;
    PDH_HQUERY gpuQuery_ = nullptr;
    PDH_HCOUNTER gpuLoadCounter_ = nullptr;
    PDH_HQUERY gpuMemoryQuery_ = nullptr;
    PDH_HCOUNTER gpuDedicatedCounter_ = nullptr;

    ULONG selectedIndex_ = 0;
    uint64_t previousInOctets_ = 0;
    uint64_t previousOutOctets_ = 0;
    std::chrono::steady_clock::time_point previousNetworkTick_{};
    std::chrono::steady_clock::time_point lastFast_{};
    std::chrono::steady_clock::time_point lastSensors_{};
    std::chrono::steady_clock::time_point lastNetwork_{};
    std::chrono::steady_clock::time_point lastStorage_{};
};

bool WmiSession::Initialize() {
    if (initialized_) {
        return true;
    }
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE) {
        return false;
    }
    comInitialized_ = SUCCEEDED(coInit);
    const HRESULT security = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE, nullptr);
    if (FAILED(security) && security != RPC_E_TOO_LATE) {
        return false;
    }
    const HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(&locator_));
    if (FAILED(hr)) {
        return false;
    }
    initialized_ = true;
    return true;
}

WmiSession::~WmiSession() {
    for (auto& pair : services_) {
        if (pair.second != nullptr) {
            pair.second->Release();
        }
    }
    if (locator_ != nullptr) {
        locator_->Release();
    }
    if (comInitialized_) {
        CoUninitialize();
    }
}

std::optional<std::wstring> WmiSession::QueryFirstString(
    const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property) {
    VARIANT value{};
    VariantInit(&value);
    if (!QueryFirstVariant(namespaceName, query, property, value)) {
        return std::nullopt;
    }
    std::optional<std::wstring> result;
    if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
        result = std::wstring(value.bstrVal);
    }
    VariantClear(&value);
    return result;
}

std::optional<double> WmiSession::QueryFirstDouble(
    const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property) {
    VARIANT value{};
    VariantInit(&value);
    if (!QueryFirstVariant(namespaceName, query, property, value)) {
        return std::nullopt;
    }
    const std::optional<double> result = VariantToDouble(value);
    VariantClear(&value);
    return result;
}

std::vector<SensorRecord> WmiSession::EnumerateSensors(const std::wstring& namespaceName) {
    std::vector<SensorRecord> sensors;
    IWbemServices* services = GetServices(namespaceName);
    if (services == nullptr) {
        return sensors;
    }
    BSTR language = SysAllocString(L"WQL");
    BSTR queryText = SysAllocString(L"SELECT Name, Identifier, Parent, SensorType, Value FROM Sensor");
    IEnumWbemClassObject* enumerator = nullptr;
    const HRESULT hr = services->ExecQuery(
        language, queryText,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &enumerator);
    SysFreeString(language);
    SysFreeString(queryText);
    if (FAILED(hr) || enumerator == nullptr) {
        return sensors;
    }
    while (true) {
        IWbemClassObject* object = nullptr;
        ULONG returned = 0;
        const HRESULT nextHr = enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
        if (FAILED(nextHr) || returned == 0 || object == nullptr) {
            break;
        }
        VARIANT name{};
        VARIANT identifier{};
        VARIANT parent{};
        VARIANT sensorType{};
        VARIANT value{};
        VariantInit(&name);
        VariantInit(&identifier);
        VariantInit(&parent);
        VariantInit(&sensorType);
        VariantInit(&value);
        object->Get(L"Name", 0, &name, nullptr, nullptr);
        object->Get(L"Identifier", 0, &identifier, nullptr, nullptr);
        object->Get(L"Parent", 0, &parent, nullptr, nullptr);
        object->Get(L"SensorType", 0, &sensorType, nullptr, nullptr);
        object->Get(L"Value", 0, &value, nullptr, nullptr);
        const auto numericValue = VariantToDouble(value);
        if (numericValue.has_value()) {
            SensorRecord sensor;
            sensor.sensorNamespace = namespaceName;
            sensor.name = VariantToString(name).value_or(L"");
            sensor.identifier = VariantToString(identifier).value_or(L"");
            sensor.parent = VariantToString(parent).value_or(L"");
            sensor.sensorType = VariantToString(sensorType).value_or(L"");
            sensor.value = *numericValue;
            sensors.push_back(std::move(sensor));
        }
        VariantClear(&name);
        VariantClear(&identifier);
        VariantClear(&parent);
        VariantClear(&sensorType);
        VariantClear(&value);
        object->Release();
    }
    enumerator->Release();
    return sensors;
}

bool WmiSession::QueryFirstVariant(
    const std::wstring& namespaceName, const std::wstring& query,
    const std::wstring& property, VARIANT& value) {
    IWbemServices* services = GetServices(namespaceName);
    if (services == nullptr) {
        return false;
    }
    BSTR language = SysAllocString(L"WQL");
    BSTR queryText = SysAllocString(query.c_str());
    IEnumWbemClassObject* enumerator = nullptr;
    const HRESULT hr = services->ExecQuery(
        language, queryText,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &enumerator);
    SysFreeString(language);
    SysFreeString(queryText);
    if (FAILED(hr) || enumerator == nullptr) {
        return false;
    }
    IWbemClassObject* object = nullptr;
    ULONG returned = 0;
    const HRESULT nextHr = enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
    if (FAILED(nextHr) || returned == 0 || object == nullptr) {
        enumerator->Release();
        return false;
    }
    const HRESULT getHr = object->Get(property.c_str(), 0, &value, nullptr, nullptr);
    object->Release();
    enumerator->Release();
    return SUCCEEDED(getHr);
}

IWbemServices* WmiSession::GetServices(const std::wstring& namespaceName) {
    if (!initialized_ && !Initialize()) {
        return nullptr;
    }
    const auto found = services_.find(namespaceName);
    if (found != services_.end()) {
        return found->second;
    }
    BSTR ns = SysAllocString(namespaceName.c_str());
    IWbemServices* services = nullptr;
    HRESULT hr = locator_->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(ns);
    if (FAILED(hr) || services == nullptr) {
        return nullptr;
    }
    hr = CoSetProxyBlanket(
        services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        services->Release();
        return nullptr;
    }
    services_[namespaceName] = services;
    return services;
}

TelemetryCollector::TelemetryCollector() : impl_(std::make_unique<Impl>()) {}

TelemetryCollector::~TelemetryCollector() = default;

TelemetryCollector::TelemetryCollector(TelemetryCollector&&) noexcept = default;

TelemetryCollector& TelemetryCollector::operator=(TelemetryCollector&&) noexcept = default;

bool TelemetryCollector::Initialize(const AppConfig& config) {
    impl_->config_ = config;
    impl_->snapshot_.network.uploadHistory.assign(60, 0.0);
    impl_->snapshot_.network.downloadHistory.assign(60, 0.0);
    WSADATA wsaData{};
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    const auto cpuName = impl_->wmi_.QueryFirstString(L"root\\cimv2", L"SELECT Name FROM Win32_Processor", L"Name");
    if (cpuName.has_value()) {
        impl_->snapshot_.cpu.name = *cpuName;
    }
    const auto gpuName = impl_->wmi_.QueryFirstString(L"root\\cimv2", L"SELECT Name FROM Win32_VideoController", L"Name");
    if (gpuName.has_value()) {
        impl_->snapshot_.gpu.name = *gpuName;
    }
    const auto gpuRam = impl_->wmi_.QueryFirstDouble(L"root\\cimv2", L"SELECT AdapterRAM FROM Win32_VideoController", L"AdapterRAM");
    if (gpuRam.has_value() && *gpuRam > 0.0) {
        impl_->snapshot_.gpu.vram.totalGb = *gpuRam / (1024.0 * 1024.0 * 1024.0);
    }

    PdhOpenQueryW(nullptr, 0, &impl_->cpuQuery_);
    AddCounterCompat(impl_->cpuQuery_, L"\\Processor Information(_Total)\\% Processor Utility", &impl_->cpuLoadCounter_);
    if (impl_->cpuLoadCounter_ == nullptr) {
        AddCounterCompat(impl_->cpuQuery_, L"\\Processor(_Total)\\% Processor Time", &impl_->cpuLoadCounter_);
    }
    AddCounterCompat(impl_->cpuQuery_, L"\\Processor Information(_Total)\\Processor Frequency", &impl_->cpuFrequencyCounter_);
    PdhCollectQueryData(impl_->cpuQuery_);

    PdhOpenQueryW(nullptr, 0, &impl_->gpuQuery_);
    AddCounterCompat(impl_->gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", &impl_->gpuLoadCounter_);
    PdhCollectQueryData(impl_->gpuQuery_);

    PdhOpenQueryW(nullptr, 0, &impl_->gpuMemoryQuery_);
    AddCounterCompat(impl_->gpuMemoryQuery_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", &impl_->gpuDedicatedCounter_);
    PdhCollectQueryData(impl_->gpuMemoryQuery_);

    impl_->EnumerateDrives();
    impl_->UpdateNetworkState(true);
    impl_->UpdateMemory();
    impl_->UpdateSensors();
    GetLocalTime(&impl_->snapshot_.now);
    return true;
}

const SystemSnapshot& TelemetryCollector::Snapshot() const {
    return impl_->snapshot_;
}

void TelemetryCollector::UpdateSnapshot() {
    const auto now = std::chrono::steady_clock::now();
    if (now - impl_->lastFast_ >= std::chrono::milliseconds(750)) {
        impl_->UpdateCpu();
        impl_->UpdateGpu();
        impl_->lastFast_ = now;
    }
    if (now - impl_->lastNetwork_ >= std::chrono::milliseconds(500)) {
        impl_->UpdateNetworkState(false);
        impl_->lastNetwork_ = now;
    }
    if (now - impl_->lastSensors_ >= std::chrono::seconds(1)) {
        impl_->UpdateSensors();
        impl_->UpdateMemory();
        impl_->lastSensors_ = now;
    }
    if (now - impl_->lastStorage_ >= std::chrono::seconds(8)) {
        impl_->RefreshDriveUsage();
        impl_->lastStorage_ = now;
    }
    GetLocalTime(&impl_->snapshot_.now);
}

void TelemetryCollector::Impl::UpdateCpu() {
    if (cpuQuery_ == nullptr) {
        return;
    }
    PdhCollectQueryData(cpuQuery_);

    PDH_FMT_COUNTERVALUE value{};
    if (cpuLoadCounter_ != nullptr &&
        PdhGetFormattedCounterValue(cpuLoadCounter_, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
        snapshot_.cpu.loadPercent = std::clamp(value.doubleValue, 0.0, 100.0);
    }
    if (cpuFrequencyCounter_ != nullptr &&
        PdhGetFormattedCounterValue(cpuFrequencyCounter_, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
        snapshot_.cpu.clock.value = value.doubleValue / 1000.0;
        snapshot_.cpu.clock.unit = L"GHz";
    }
}

double TelemetryCollector::Impl::SumCounterArray(PDH_HCOUNTER counter, bool require3d) {
    if (counter == nullptr) {
        return 0.0;
    }
    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA) {
        return 0.0;
    }
    std::vector<BYTE> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        return 0.0;
    }
    double total = 0.0;
    for (DWORD i = 0; i < itemCount; ++i) {
        const std::wstring instance = items[i].szName != nullptr ? items[i].szName : L"";
        if (require3d && instance.find(L"engtype_3D") == std::wstring::npos) {
            continue;
        }
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS) {
            continue;
        }
        total += items[i].FmtValue.doubleValue;
    }
    return total;
}

void TelemetryCollector::Impl::UpdateGpu() {
    if (gpuQuery_ != nullptr) {
        PdhCollectQueryData(gpuQuery_);
        const double load3d = SumCounterArray(gpuLoadCounter_, true);
        const double loadAll = SumCounterArray(gpuLoadCounter_, false);
        snapshot_.gpu.loadPercent = std::clamp(load3d > 0.0 ? load3d : loadAll, 0.0, 100.0);
    }
    if (gpuMemoryQuery_ != nullptr) {
        PdhCollectQueryData(gpuMemoryQuery_);
        const double bytes = SumCounterArray(gpuDedicatedCounter_, false);
        snapshot_.gpu.vram.usedGb = bytes / (1024.0 * 1024.0 * 1024.0);
    }
}

void TelemetryCollector::Impl::UpdateMemory() {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        snapshot_.cpu.memory.totalGb = memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        snapshot_.cpu.memory.usedGb =
            (memory.ullTotalPhys - memory.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
}

std::optional<double> TelemetryCollector::Impl::QuerySensor(const std::wstring& key) {
    const auto found = config_.sensors.find(ToLower(key));
    if (found == config_.sensors.end() || !found->second.IsConfigured()) {
        return std::nullopt;
    }
    const auto& binding = found->second;
    for (const auto& ns : binding.namespaces) {
        const std::wstring query = L"SELECT " + binding.valueField + L" FROM Sensor WHERE " +
            binding.matchField + L"='" + EscapeWql(binding.matchValue) + L"'";
        const auto value = wmi_.QueryFirstDouble(ns, query, binding.valueField);
        if (value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

std::vector<SensorRecord> TelemetryCollector::Impl::LoadSensors() {
    const auto namespaces = GatherSensorNamespaces(config_);
    for (const auto& ns : namespaces) {
        auto sensors = wmi_.EnumerateSensors(ns);
        if (!sensors.empty()) {
            return sensors;
        }
    }
    return {};
}

std::optional<double> TelemetryCollector::Impl::QueryAutoSensor(
    const std::wstring& key, const std::vector<SensorRecord>& sensors) {
    if (key == L"cpu_temp") {
        return FindBestAutoSensorValue(sensors, L"Temperature", {L"cpu"}, {L"package", L"tctl", L"tdie", L"core average"}, {L"vrm", L"gpu"}, {});
    }
    if (key == L"cpu_power") {
        return FindBestAutoSensorValue(sensors, L"Power", {L"cpu"}, {L"package", L"total"}, {L"core #", L"gpu"}, {});
    }
    if (key == L"cpu_fan") {
        auto value = FindBestAutoSensorValue(sensors, L"Fan", {L"cpu"}, {L"fan", L"pump"}, {L"gpu", L"case", L"chassis"}, {});
        if (!value.has_value()) {
            value = FindBestAutoSensorValue(sensors, L"Fan", {}, {L"cpu fan", L"pump"}, {L"gpu", L"case", L"chassis"}, {});
        }
        return value;
    }
    if (key == L"cpu_clock") {
        return FindBestAutoSensorValue(sensors, L"Clock", {L"cpu"}, {L"core average", L"cpu core", L"core #1", L"bus speed"}, {L"gpu", L"memory"}, {});
    }
    if (key == L"gpu_temp") {
        return FindBestAutoSensorValue(sensors, L"Temperature", {L"gpu"}, {L"core", L"hot spot", L"hotspot", L"edge"}, {L"cpu", L"memory"}, {});
    }
    if (key == L"gpu_power") {
        return FindBestAutoSensorValue(sensors, L"Power", {L"gpu"}, {L"package", L"board", L"asic", L"core"}, {L"cpu"}, {});
    }
    if (key == L"gpu_fan") {
        return FindBestAutoSensorValue(sensors, L"Fan", {L"gpu"}, {L"fan"}, {L"cpu", L"case", L"chassis"}, {});
    }
    if (key == L"gpu_clock") {
        return FindBestAutoSensorValue(sensors, L"Clock", {L"gpu"}, {L"core", L"graphics"}, {L"cpu", L"memory"}, {});
    }
    return std::nullopt;
}

void TelemetryCollector::Impl::UpdateSensors() {
    const auto sensors = LoadSensors();
    auto resolve = [&](const std::wstring& key) -> std::optional<double> {
        if (const auto configured = QuerySensor(key); configured.has_value()) {
            return configured;
        }
        return QueryAutoSensor(key, sensors);
    };

    snapshot_.cpu.temperature.value = resolve(L"cpu_temp");
    snapshot_.cpu.power.value = resolve(L"cpu_power");
    snapshot_.cpu.fan.value = resolve(L"cpu_fan");
    if (!snapshot_.cpu.clock.value.has_value()) {
        if (const auto cpuClock = resolve(L"cpu_clock"); cpuClock.has_value()) {
            snapshot_.cpu.clock.value = *cpuClock / 1000.0;
            snapshot_.cpu.clock.unit = L"GHz";
        }
    }

    snapshot_.gpu.temperature.value = resolve(L"gpu_temp");
    snapshot_.gpu.power.value = resolve(L"gpu_power");
    snapshot_.gpu.fan.value = resolve(L"gpu_fan");
    if (const auto gpuClock = resolve(L"gpu_clock"); gpuClock.has_value()) {
        snapshot_.gpu.clock.value = *gpuClock;
        snapshot_.gpu.clock.unit = L"MHz";
    }
}

void TelemetryCollector::Impl::EnumerateDrives() {
    for (const auto& drive : config_.driveLetters) {
        if (!drive.empty()) {
            snapshot_.drives.push_back(DriveInfo{drive.substr(0, 1) + L":"});
        }
    }
    RefreshDriveUsage();
}

void TelemetryCollector::Impl::RefreshDriveUsage() {
    for (auto& drive : snapshot_.drives) {
        const std::wstring root = drive.label + L"\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) {
            continue;
        }

        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        if (!GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, nullptr) || totalBytes.QuadPart == 0) {
            continue;
        }

        const double totalGb = totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        const double freeGb = freeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        drive.freeGb = freeGb;
        drive.usedPercent = std::clamp((1.0 - (freeGb / totalGb)) * 100.0, 0.0, 100.0);
    }
}

void TelemetryCollector::Impl::PushHistory(std::vector<double>& history, double value) {
    if (history.empty()) {
        return;
    }
    history.erase(history.begin());
    history.push_back(value);
}

std::wstring TelemetryCollector::Impl::FindAdapterIp(ULONG interfaceIndex) {
    ULONG size = 0;
    GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &size);
    std::vector<BYTE> buffer(size);
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr, addresses, &size) != NO_ERROR) {
        return L"N/A";
    }

    for (auto* current = addresses; current != nullptr; current = current->Next) {
        if (current->IfIndex != interfaceIndex) {
            continue;
        }
        for (auto* unicast = current->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            wchar_t address[128];
            DWORD length = ARRAYSIZE(address);
            if (WSAAddressToStringW(unicast->Address.lpSockaddr,
                    static_cast<DWORD>(unicast->Address.iSockaddrLength),
                    nullptr, address, &length) == 0) {
                std::wstring ip = address;
                if (ip.find(L':') == std::wstring::npos) {
                    return ip;
                }
            }
        }
    }

    return L"N/A";
}

void TelemetryCollector::Impl::UpdateNetworkState(bool initializeOnly) {
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || table == nullptr) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    MIB_IF_ROW2* selected = nullptr;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        auto& row = table->Table[i];
        const bool isCandidate =
            row.Type != IF_TYPE_SOFTWARE_LOOPBACK &&
            row.OperStatus == IfOperStatusUp &&
            (config_.networkAdapter.empty() ||
                ContainsInsensitive(row.Alias, config_.networkAdapter) ||
                ContainsInsensitive(row.Description, config_.networkAdapter));
        if (isCandidate) {
            selected = &row;
            break;
        }
    }

    if (selected != nullptr) {
        snapshot_.network.adapterName = selected->Alias[0] != L'\0' ? selected->Alias : selected->Description;
        if (selectedIndex_ != selected->InterfaceIndex) {
            selectedIndex_ = selected->InterfaceIndex;
            previousInOctets_ = selected->InOctets;
            previousOutOctets_ = selected->OutOctets;
            previousNetworkTick_ = now;
        } else if (!initializeOnly) {
            const double seconds = std::chrono::duration<double>(now - previousNetworkTick_).count();
            if (seconds > 0.0) {
                snapshot_.network.downloadMbps =
                    ((selected->InOctets - previousInOctets_) / seconds) / (1024.0 * 1024.0);
                snapshot_.network.uploadMbps =
                    ((selected->OutOctets - previousOutOctets_) / seconds) / (1024.0 * 1024.0);
                PushHistory(snapshot_.network.uploadHistory, snapshot_.network.uploadMbps);
                PushHistory(snapshot_.network.downloadHistory, snapshot_.network.downloadMbps);
            }
            previousInOctets_ = selected->InOctets;
            previousOutOctets_ = selected->OutOctets;
            previousNetworkTick_ = now;
        }

        snapshot_.network.ipAddress = FindAdapterIp(selected->InterfaceIndex);
    }

    FreeMibTable(table);
}
