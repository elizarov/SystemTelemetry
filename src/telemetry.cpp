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
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <cstdio>
#include <string>
#include <vector>

#include "gpu_vendor.h"
#include "telemetry.h"

namespace {

struct WmiDiagnostic {
    std::wstring namespaceName;
    std::wstring query;
    HRESULT result = S_OK;
    std::wstring detail;
};

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring FormatHresult(HRESULT hr) {
    wchar_t buffer[32];
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

std::wstring FormatScalarMetric(const ScalarMetric& metric, int precision) {
    if (!metric.value.has_value()) {
        return L"N/A";
    }
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.*f %ls", precision, *metric.value, metric.unit.c_str());
    return buffer;
}

std::wstring FormatMemoryMetric(const MemoryMetric& metric) {
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.1f / %.1f GB", metric.usedGb, metric.totalGb);
    return buffer;
}

void AppendWideLine(std::wstring& output, const std::wstring& text) {
    output += text;
    output += L"\r\n";
}

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

class WmiSession {
public:
    bool Initialize();
    ~WmiSession();

    std::optional<std::wstring> QueryFirstString(
        const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property);
    std::optional<double> QueryFirstDouble(
        const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property);
    std::vector<WmiDiagnostic> Diagnostics() const;
    void ClearDiagnostics();

private:
    bool QueryFirstVariant(
        const std::wstring& namespaceName, const std::wstring& query,
        const std::wstring& property, VARIANT& value);
    IWbemServices* GetServices(const std::wstring& namespaceName);
    void RecordDiagnostic(
        const std::wstring& namespaceName, const std::wstring& query, HRESULT result, const std::wstring& detail);

    bool initialized_ = false;
    bool comInitialized_ = false;
    IWbemLocator* locator_ = nullptr;
    std::map<std::wstring, IWbemServices*> services_;
    std::vector<WmiDiagnostic> diagnostics_;
};

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

}  // namespace

struct TelemetryCollector::Impl {
    ~Impl();

    void UpdateCpu();
    void UpdateGpu();
    void UpdateMemory();
    void ApplyGpuVendorSample(const GpuVendorTelemetrySample& sample);
    void EnumerateDrives();
    void RefreshDriveUsage();
    void UpdateNetworkState(bool initializeOnly);
    std::wstring DumpText() const;
    double SumCounterArray(PDH_HCOUNTER counter, bool require3d);
    std::wstring FindAdapterIp(ULONG interfaceIndex);
    static void PushHistory(std::vector<double>& history, double value);

    AppConfig config_;
    SystemSnapshot snapshot_;
    WmiSession wmi_;
    std::unique_ptr<GpuVendorTelemetryProvider> gpuProvider_;
    std::wstring gpuProviderName_ = L"None";
    std::wstring gpuProviderDiagnostics_ = L"Provider not initialized.";
    bool gpuProviderAvailable_ = false;

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
    std::chrono::steady_clock::time_point lastDetails_{};
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
    const HRESULT hr = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
        reinterpret_cast<void**>(&locator_));
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

std::vector<WmiDiagnostic> WmiSession::Diagnostics() const {
    return diagnostics_;
}

void WmiSession::ClearDiagnostics() {
    diagnostics_.clear();
}

std::optional<std::wstring> WmiSession::QueryFirstString(
    const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property) {
    VARIANT value{};
    VariantInit(&value);
    if (!QueryFirstVariant(namespaceName, query, property, value)) {
        return std::nullopt;
    }
    const std::optional<std::wstring> result = VariantToString(value);
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

bool WmiSession::QueryFirstVariant(
    const std::wstring& namespaceName, const std::wstring& query,
    const std::wstring& property, VARIANT& value) {
    IWbemServices* services = GetServices(namespaceName);
    if (services == nullptr) {
        RecordDiagnostic(namespaceName, query, E_FAIL, L"Failed to open namespace");
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
        RecordDiagnostic(namespaceName, query, hr, L"ExecQuery failed");
        return false;
    }
    IWbemClassObject* object = nullptr;
    ULONG returned = 0;
    const HRESULT nextHr = enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
    if (FAILED(nextHr) || returned == 0 || object == nullptr) {
        enumerator->Release();
        RecordDiagnostic(
            namespaceName, query, nextHr,
            returned == 0 ? L"Query returned no rows" : L"Enumerator Next failed");
        return false;
    }
    const HRESULT getHr = object->Get(property.c_str(), 0, &value, nullptr, nullptr);
    object->Release();
    enumerator->Release();
    RecordDiagnostic(namespaceName, query, getHr, SUCCEEDED(getHr) ? L"OK" : L"Get(property) failed");
    return SUCCEEDED(getHr);
}

IWbemServices* WmiSession::GetServices(const std::wstring& namespaceName) {
    if (!initialized_ && !Initialize()) {
        RecordDiagnostic(namespaceName, L"", E_FAIL, L"Initialize failed");
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
        RecordDiagnostic(namespaceName, L"", hr, L"ConnectServer failed");
        return nullptr;
    }
    hr = CoSetProxyBlanket(
        services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        services->Release();
        RecordDiagnostic(namespaceName, L"", hr, L"CoSetProxyBlanket failed");
        return nullptr;
    }
    services_[namespaceName] = services;
    return services;
}

void WmiSession::RecordDiagnostic(
    const std::wstring& namespaceName, const std::wstring& query, HRESULT result, const std::wstring& detail) {
    diagnostics_.push_back(WmiDiagnostic{namespaceName, query, result, detail});
}

TelemetryCollector::Impl::~Impl() {
    if (cpuQuery_ != nullptr) {
        PdhCloseQuery(cpuQuery_);
    }
    if (gpuQuery_ != nullptr) {
        PdhCloseQuery(gpuQuery_);
    }
    if (gpuMemoryQuery_ != nullptr) {
        PdhCloseQuery(gpuMemoryQuery_);
    }
    WSACleanup();
}

TelemetryCollector::TelemetryCollector() : impl_(std::make_unique<Impl>()) {}

TelemetryCollector::~TelemetryCollector() = default;

TelemetryCollector::TelemetryCollector(TelemetryCollector&&) noexcept = default;

TelemetryCollector& TelemetryCollector::operator=(TelemetryCollector&&) noexcept = default;

bool TelemetryCollector::Initialize(const AppConfig& config) {
    impl_->config_ = config;
    impl_->wmi_.ClearDiagnostics();
    impl_->snapshot_.network.uploadHistory.assign(60, 0.0);
    impl_->snapshot_.network.downloadHistory.assign(60, 0.0);

    WSADATA wsaData{};
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    const auto cpuName = impl_->wmi_.QueryFirstString(
        L"root\\cimv2", L"SELECT Name FROM Win32_Processor", L"Name");
    if (cpuName.has_value()) {
        impl_->snapshot_.cpu.name = *cpuName;
    }

    const auto gpuName = impl_->wmi_.QueryFirstString(
        L"root\\cimv2", L"SELECT Name FROM Win32_VideoController", L"Name");
    if (gpuName.has_value()) {
        impl_->snapshot_.gpu.name = *gpuName;
    }

    const auto gpuRam = impl_->wmi_.QueryFirstDouble(
        L"root\\cimv2", L"SELECT AdapterRAM FROM Win32_VideoController", L"AdapterRAM");
    if (gpuRam.has_value() && *gpuRam > 0.0) {
        impl_->snapshot_.gpu.vram.totalGb = *gpuRam / (1024.0 * 1024.0 * 1024.0);
    }

    impl_->gpuProvider_ = CreateGpuVendorTelemetryProvider();
    if (impl_->gpuProvider_ != nullptr) {
        if (impl_->gpuProvider_->Initialize()) {
            impl_->ApplyGpuVendorSample(impl_->gpuProvider_->Sample());
        } else {
            impl_->gpuProviderName_ = L"AMD ADL";
            impl_->gpuProviderDiagnostics_ = L"Provider initialization failed.";
        }
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
    impl_->UpdateCpu();
    impl_->UpdateGpu();
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
    if (now - impl_->lastDetails_ >= std::chrono::seconds(1)) {
        impl_->UpdateMemory();
        impl_->lastDetails_ = now;
    }
    if (now - impl_->lastStorage_ >= std::chrono::seconds(8)) {
        impl_->RefreshDriveUsage();
        impl_->lastStorage_ = now;
    }
    GetLocalTime(&impl_->snapshot_.now);
}

std::wstring TelemetryCollector::DumpText() const {
    return impl_->DumpText();
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

void TelemetryCollector::Impl::ApplyGpuVendorSample(const GpuVendorTelemetrySample& sample) {
    gpuProviderName_ = sample.providerName.empty() ? L"None" : sample.providerName;
    gpuProviderDiagnostics_ = sample.diagnostics.empty() ? L"(none)" : sample.diagnostics;
    gpuProviderAvailable_ = sample.available;

    if (sample.name.has_value() && !sample.name->empty()) {
        snapshot_.gpu.name = *sample.name;
    }
    snapshot_.gpu.temperature.value = sample.temperatureC;
    snapshot_.gpu.clock.value = sample.coreClockMhz;
    snapshot_.gpu.clock.unit = L"MHz";
    snapshot_.gpu.fan.value = sample.fanRpm;
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
    if (gpuProvider_ != nullptr) {
        ApplyGpuVendorSample(gpuProvider_->Sample());
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

std::wstring TelemetryCollector::Impl::DumpText() const {
    std::wstring output;
    const auto now = snapshot_.now;
    wchar_t dateTime[64];
    swprintf_s(dateTime, L"%04d-%02d-%02d %02d:%02d:%02d",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    AppendWideLine(output, L"System Telemetry Dump");
    AppendWideLine(output, L"=====================");
    AppendWideLine(output, std::wstring(L"Timestamp: ") + dateTime);
    AppendWideLine(output, L"");

    AppendWideLine(output, L"[CPU]");
    AppendWideLine(output, std::wstring(L"Name: ") + snapshot_.cpu.name);
    {
        wchar_t buffer[64];
        swprintf_s(buffer, L"Load: %.2f%%", snapshot_.cpu.loadPercent);
        AppendWideLine(output, buffer);
    }
    AppendWideLine(output, std::wstring(L"Clock: ") + FormatScalarMetric(snapshot_.cpu.clock, 2));
    AppendWideLine(output, std::wstring(L"Memory: ") + FormatMemoryMetric(snapshot_.cpu.memory));
    AppendWideLine(output, L"");

    AppendWideLine(output, L"[GPU]");
    AppendWideLine(output, std::wstring(L"Name: ") + snapshot_.gpu.name);
    {
        wchar_t buffer[64];
        swprintf_s(buffer, L"Load: %.2f%%", snapshot_.gpu.loadPercent);
        AppendWideLine(output, buffer);
    }
    AppendWideLine(output, std::wstring(L"Temperature: ") + FormatScalarMetric(snapshot_.gpu.temperature, 1));
    AppendWideLine(output, std::wstring(L"Clock: ") + FormatScalarMetric(snapshot_.gpu.clock, 0));
    AppendWideLine(output, std::wstring(L"Fan: ") + FormatScalarMetric(snapshot_.gpu.fan, 0));
    AppendWideLine(output, std::wstring(L"VRAM: ") + FormatMemoryMetric(snapshot_.gpu.vram));
    AppendWideLine(output, L"");

    AppendWideLine(output, L"[GPU Vendor Provider]");
    AppendWideLine(output, std::wstring(L"Name: ") + gpuProviderName_);
    AppendWideLine(output, std::wstring(L"Available: ") + (gpuProviderAvailable_ ? L"yes" : L"no"));
    AppendWideLine(output, std::wstring(L"Diagnostics: ") + gpuProviderDiagnostics_);
    AppendWideLine(output, L"");

    AppendWideLine(output, L"[Network]");
    AppendWideLine(output, std::wstring(L"Adapter: ") + snapshot_.network.adapterName);
    AppendWideLine(output, std::wstring(L"IP: ") + snapshot_.network.ipAddress);
    {
        wchar_t buffer[64];
        swprintf_s(buffer, L"Upload: %.3f MB/s", snapshot_.network.uploadMbps);
        AppendWideLine(output, buffer);
        swprintf_s(buffer, L"Download: %.3f MB/s", snapshot_.network.downloadMbps);
        AppendWideLine(output, buffer);
    }
    AppendWideLine(output, L"");

    AppendWideLine(output, L"[Storage]");
    if (snapshot_.drives.empty()) {
        AppendWideLine(output, L"(none)");
    } else {
        for (const auto& drive : snapshot_.drives) {
            wchar_t buffer[128];
            swprintf_s(buffer, L"%ls used=%.1f%% free=%.1f GB", drive.label.c_str(), drive.usedPercent, drive.freeGb);
            AppendWideLine(output, buffer);
        }
    }
    AppendWideLine(output, L"");

    AppendWideLine(output, L"[WMI Diagnostics]");
    const auto diagnostics = wmi_.Diagnostics();
    if (diagnostics.empty()) {
        AppendWideLine(output, L"(none)");
    } else {
        for (const auto& diagnostic : diagnostics) {
            std::wstringstream line;
            line << L"ns=" << diagnostic.namespaceName
                 << L" hr=" << FormatHresult(diagnostic.result)
                 << L" detail=" << diagnostic.detail;
            if (!diagnostic.query.empty()) {
                line << L" query=" << diagnostic.query;
            }
            AppendWideLine(output, line.str());
        }
    }
    return output;
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
            if (WSAAddressToStringW(
                    unicast->Address.lpSockaddr, static_cast<DWORD>(unicast->Address.iSockaddrLength),
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
