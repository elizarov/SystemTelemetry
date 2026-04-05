#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <dxgi.h>
#include <intrin.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "board_vendor.h"
#include "gpu_vendor.h"
#include "snapshot_dump.h"
#include "telemetry.h"
#include "trace.h"
#include "utf8.h"

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string FormatScalarMetric(const ScalarMetric& metric, int precision) {
    if (!metric.value.has_value()) {
        return "N/A";
    }
    char buffer[64];
    sprintf_s(buffer, "%.*f %s", precision, *metric.value, metric.unit.c_str());
    return buffer;
}

std::vector<NamedScalarMetric> CreateRequestedBoardMetrics(const std::vector<std::string>& names, const char* unit) {
    std::vector<NamedScalarMetric> metrics;
    metrics.reserve(names.size());
    for (const auto& name : names) {
        metrics.push_back(NamedScalarMetric{name, ScalarMetric{std::nullopt, unit}});
    }
    return metrics;
}

typedef PDH_STATUS(WINAPI* PdhAddEnglishCounterWFn)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);

PDH_STATUS AddCounterCompat(PDH_HQUERY query, const wchar_t* path, PDH_HCOUNTER* counter) {
    static PdhAddEnglishCounterWFn addEnglish = reinterpret_cast<PdhAddEnglishCounterWFn>(
        GetProcAddress(GetModuleHandleW(L"pdh.dll"), "PdhAddEnglishCounterW"));
    if (addEnglish != nullptr) {
        return addEnglish(query, path, 0, counter);
    }
    return PdhAddCounterW(query, path, 0, counter);
}

bool ContainsInsensitive(const std::wstring& value, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(Utf8FromWide(value)).find(ToLower(needle)) != std::string::npos;
}

bool EqualsInsensitive(const std::wstring& value, const std::string& needle) {
    if (needle.empty()) {
        return false;
    }
    return ToLower(Utf8FromWide(value)) == ToLower(needle);
}

std::string TrimAsciiWhitespace(std::string value) {
    auto notSpace = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    const auto begin = std::find_if(value.begin(), value.end(), notSpace);
    if (begin == value.end()) {
        return "";
    }

    const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return std::string(begin, end);
}

std::string CollapseAsciiWhitespace(std::string value) {
    std::string collapsed;
    collapsed.reserve(value.size());

    bool pendingSpace = false;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            pendingSpace = !collapsed.empty();
            continue;
        }
        if (pendingSpace) {
            collapsed.push_back(' ');
            pendingSpace = false;
        }
        collapsed.push_back(static_cast<char>(ch));
    }
    return collapsed;
}

std::optional<std::string> ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD bytes = 0;
    const LONG probe = RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, nullptr, &bytes);
    if (probe != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return std::nullopt;
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    const LONG status = RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, value.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }

    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return Utf8FromWide(value);
}

std::string DetectCpuNameFromCpuid() {
    int maxExtendedLeaf[4]{};
    __cpuid(maxExtendedLeaf, 0x80000000);
    if (static_cast<unsigned int>(maxExtendedLeaf[0]) < 0x80000004) {
        return "";
    }

    std::array<int, 12> brandWords{};
    for (int i = 0; i < 3; ++i) {
        int leafData[4]{};
        __cpuid(leafData, 0x80000002 + i);
        for (int j = 0; j < 4; ++j) {
            brandWords[static_cast<size_t>(i) * 4 + static_cast<size_t>(j)] = leafData[j];
        }
    }

    std::string brand(reinterpret_cast<const char*>(brandWords.data()), brandWords.size() * sizeof(int));
    const size_t terminator = brand.find('\0');
    if (terminator != std::string::npos) {
        brand.resize(terminator);
    }
    return CollapseAsciiWhitespace(TrimAsciiWhitespace(brand));
}

std::string DetectCpuName() {
    const std::string cpuidName = DetectCpuNameFromCpuid();
    if (!cpuidName.empty()) {
        return cpuidName;
    }

    const auto registryName = ReadRegistryString(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString");
    if (registryName.has_value()) {
        return CollapseAsciiWhitespace(TrimAsciiWhitespace(*registryName));
    }
    return "";
}

struct AdapterSelectionInfo {
    bool matched = false;
    bool hasIpv4 = false;
    bool hasGateway = false;
    std::string ipAddress = "N/A";
};

bool AdapterMatchesRow(const IP_ADAPTER_ADDRESSES& adapter, const MIB_IF_ROW2& row) {
    return adapter.Luid.Value == row.InterfaceLuid.Value ||
        adapter.IfIndex == row.InterfaceIndex ||
        adapter.Ipv6IfIndex == row.InterfaceIndex ||
        (adapter.FriendlyName != nullptr && _wcsicmp(adapter.FriendlyName, row.Alias) == 0) ||
        (adapter.Description != nullptr && _wcsicmp(adapter.Description, row.Description) == 0);
}

bool HasUsableGateway(const IP_ADAPTER_ADDRESSES& adapter) {
    for (auto* gateway = adapter.FirstGatewayAddress; gateway != nullptr; gateway = gateway->Next) {
        const sockaddr* address = gateway->Address.lpSockaddr;
        if (address == nullptr) {
            continue;
        }
        if (address->sa_family == AF_INET) {
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
            if (ipv4->sin_addr.S_un.S_addr != 0) {
                return true;
            }
        } else if (address->sa_family == AF_INET6) {
            const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
            static const IN6_ADDR zeroAddress{};
            if (memcmp(&ipv6->sin6_addr, &zeroAddress, sizeof(zeroAddress)) != 0) {
                return true;
            }
        }
    }
    return false;
}

AdapterSelectionInfo BuildAdapterSelectionInfo(const MIB_IF_ROW2& row, const IP_ADAPTER_ADDRESSES* addresses) {
    AdapterSelectionInfo info;
    for (auto* current = addresses; current != nullptr; current = current->Next) {
        if (!AdapterMatchesRow(*current, row)) {
            continue;
        }

        info.matched = true;
        info.hasGateway = HasUsableGateway(*current);
        for (auto* unicast = current->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr || unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }

            wchar_t address[128];
            DWORD length = ARRAYSIZE(address);
            if (WSAAddressToStringW(
                    unicast->Address.lpSockaddr,
                    static_cast<DWORD>(unicast->Address.iSockaddrLength),
                    nullptr,
                    address,
                    &length) == 0) {
                info.hasIpv4 = true;
                info.ipAddress = Utf8FromWide(address);
                break;
            }
        }
        break;
    }
    return info;
}

}  // namespace

struct TelemetryCollector::Impl {
    ~Impl();

    void UpdateCpu();
    void UpdateGpu();
    void InitializeGpuAdapterInfo();
    void UpdateMemory();
    void ApplyBoardVendorSample(const BoardVendorTelemetrySample& sample);
    void ApplyGpuVendorSample(const GpuVendorTelemetrySample& sample);
    void EnumerateDrives();
    void UpdateStorageThroughput(bool initializeOnly);
    void RefreshDriveUsage();
    void UpdateNetworkState(bool initializeOnly);
    double SumCounterArray(PDH_HCOUNTER counter, bool require3d);
    static void PushHistory(std::vector<double>& history, double value);
    void Trace(const char* text) const;
    void Trace(const std::string& text) const;

    AppConfig config_;
    SystemSnapshot snapshot_;
    tracing::Trace trace_;
    std::unique_ptr<GpuVendorTelemetryProvider> gpuProvider_;
    std::unique_ptr<BoardVendorTelemetryProvider> boardProvider_;
    std::string gpuProviderName_ = "None";
    std::string gpuProviderDiagnostics_ = "Provider not initialized.";
    bool gpuProviderAvailable_ = false;
    std::string boardProviderName_ = "None";
    std::string boardProviderDiagnostics_ = "Provider not initialized.";
    BoardVendorTelemetrySample boardProviderSample_{};
    bool boardProviderAvailable_ = false;

    PDH_HQUERY cpuQuery_ = nullptr;
    PDH_HCOUNTER cpuLoadCounter_ = nullptr;
    PDH_HCOUNTER cpuFrequencyCounter_ = nullptr;
    PDH_HQUERY gpuQuery_ = nullptr;
    PDH_HCOUNTER gpuLoadCounter_ = nullptr;
    PDH_HQUERY gpuMemoryQuery_ = nullptr;
    PDH_HCOUNTER gpuDedicatedCounter_ = nullptr;
    PDH_HQUERY storageQuery_ = nullptr;
    PDH_HCOUNTER storageReadCounter_ = nullptr;
    PDH_HCOUNTER storageWriteCounter_ = nullptr;

    ULONG selectedIndex_ = 0;
    uint64_t previousInOctets_ = 0;
    uint64_t previousOutOctets_ = 0;
    std::chrono::steady_clock::time_point previousNetworkTick_{};
    std::chrono::steady_clock::time_point lastFast_{};
    std::chrono::steady_clock::time_point lastDetails_{};
    std::chrono::steady_clock::time_point lastNetwork_{};
    std::chrono::steady_clock::time_point lastStorage_{};
};

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
    if (storageQuery_ != nullptr) {
        PdhCloseQuery(storageQuery_);
    }
    WSACleanup();
}

TelemetryCollector::TelemetryCollector() : impl_(std::make_unique<Impl>()) {}

TelemetryCollector::~TelemetryCollector() = default;

TelemetryCollector::TelemetryCollector(TelemetryCollector&&) noexcept = default;

TelemetryCollector& TelemetryCollector::operator=(TelemetryCollector&&) noexcept = default;

bool TelemetryCollector::Initialize(const AppConfig& config, std::ostream* traceStream) {
    impl_->config_ = config;
    impl_->trace_.SetOutput(traceStream);
    impl_->snapshot_.boardTemperatures = CreateRequestedBoardMetrics(config.boardTemperatureNames, "\xC2\xB0""C");
    impl_->snapshot_.boardFans = CreateRequestedBoardMetrics(config.boardFanNames, "RPM");
    impl_->snapshot_.network.uploadHistory.assign(60, 0.0);
    impl_->snapshot_.network.downloadHistory.assign(60, 0.0);
    impl_->snapshot_.storage.readHistory.assign(60, 0.0);
    impl_->snapshot_.storage.writeHistory.assign(60, 0.0);
    if (const std::string cpuName = DetectCpuName(); !cpuName.empty()) {
        impl_->snapshot_.cpu.name = cpuName;
    }

    WSADATA wsaData{};
    const int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

    impl_->trace_.Write("telemetry:initialize_begin");
    {
        char buffer[128];
        sprintf_s(buffer, "telemetry:wsa_startup result=%d version=%u.%u",
            wsaStartupResult, LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
        impl_->trace_.Write(buffer);
    }
    impl_->trace_.Write("telemetry:cpu_name value=\"" + impl_->snapshot_.cpu.name + "\"");
    impl_->gpuProvider_ = CreateGpuVendorTelemetryProvider(&impl_->trace_);
    impl_->boardProvider_ = CreateBoardVendorTelemetryProvider(&impl_->trace_);
    if (impl_->gpuProvider_ != nullptr) {
        impl_->trace_.Write("telemetry:gpu_provider_initialize_begin");
        if (impl_->gpuProvider_->Initialize()) {
            impl_->ApplyGpuVendorSample(impl_->gpuProvider_->Sample());
            impl_->trace_.Write("telemetry:gpu_provider_initialize_done provider=" + impl_->gpuProviderName_ +
                " available=" + tracing::Trace::BoolText(impl_->gpuProviderAvailable_) +
                " diagnostics=\"" + impl_->gpuProviderDiagnostics_ + "\"");
        } else {
            impl_->gpuProviderName_ = "AMD ADLX";
            impl_->gpuProviderDiagnostics_ = "Provider initialization failed.";
            impl_->trace_.Write("telemetry:gpu_provider_initialize_failed provider=" + impl_->gpuProviderName_ +
                " diagnostics=\"" + impl_->gpuProviderDiagnostics_ + "\"");
        }
    } else {
        impl_->trace_.Write("telemetry:gpu_provider_create result=null");
    }
    if (impl_->boardProvider_ != nullptr) {
        impl_->trace_.Write("telemetry:board_provider_initialize_begin");
        if (impl_->boardProvider_->Initialize(config)) {
            impl_->ApplyBoardVendorSample(impl_->boardProvider_->Sample());
            impl_->trace_.Write("telemetry:board_provider_initialize_done provider=" + impl_->boardProviderName_ +
                " available=" + tracing::Trace::BoolText(impl_->boardProviderAvailable_) +
                " diagnostics=\"" + impl_->boardProviderDiagnostics_ + "\"");
        } else {
            impl_->ApplyBoardVendorSample(impl_->boardProvider_->Sample());
            impl_->trace_.Write("telemetry:board_provider_initialize_failed provider=" + impl_->boardProviderName_ +
                " diagnostics=\"" + impl_->boardProviderDiagnostics_ + "\"");
        }
    } else {
        impl_->trace_.Write("telemetry:board_provider_create result=null");
    }

    const PDH_STATUS cpuQueryStatus = PdhOpenQueryW(nullptr, 0, &impl_->cpuQuery_);
    impl_->trace_.Write(("telemetry:pdh_open cpu_query " + tracing::Trace::FormatPdhStatus("status", cpuQueryStatus)).c_str());
    const PDH_STATUS cpuLoadStatus = AddCounterCompat(
        impl_->cpuQuery_, L"\\Processor Information(_Total)\\% Processor Utility", &impl_->cpuLoadCounter_);
    impl_->trace_.Write(("telemetry:pdh_add cpu_load path=\"\\\\Processor Information(_Total)\\\\% Processor Utility\" " +
        tracing::Trace::FormatPdhStatus("status", cpuLoadStatus)).c_str());
    if (impl_->cpuLoadCounter_ == nullptr) {
        const PDH_STATUS cpuLoadFallbackStatus = AddCounterCompat(
            impl_->cpuQuery_, L"\\Processor(_Total)\\% Processor Time", &impl_->cpuLoadCounter_);
        impl_->trace_.Write(("telemetry:pdh_add cpu_load_fallback path=\"\\\\Processor(_Total)\\\\% Processor Time\" " +
            tracing::Trace::FormatPdhStatus("status", cpuLoadFallbackStatus)).c_str());
    }
    const PDH_STATUS cpuFreqStatus = AddCounterCompat(
        impl_->cpuQuery_, L"\\Processor Information(_Total)\\Processor Frequency", &impl_->cpuFrequencyCounter_);
    impl_->trace_.Write(("telemetry:pdh_add cpu_frequency path=\"\\\\Processor Information(_Total)\\\\Processor Frequency\" " +
        tracing::Trace::FormatPdhStatus("status", cpuFreqStatus)).c_str());
    const PDH_STATUS cpuCollectStatus = PdhCollectQueryData(impl_->cpuQuery_);
    impl_->trace_.Write(("telemetry:pdh_collect cpu_query " + tracing::Trace::FormatPdhStatus("status", cpuCollectStatus)).c_str());

    const PDH_STATUS gpuQueryStatus = PdhOpenQueryW(nullptr, 0, &impl_->gpuQuery_);
    impl_->trace_.Write(("telemetry:pdh_open gpu_query " + tracing::Trace::FormatPdhStatus("status", gpuQueryStatus)).c_str());
    const PDH_STATUS gpuLoadStatus = AddCounterCompat(
        impl_->gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", &impl_->gpuLoadCounter_);
    impl_->trace_.Write(("telemetry:pdh_add gpu_load path=\"\\\\GPU Engine(*)\\\\Utilization Percentage\" " +
        tracing::Trace::FormatPdhStatus("status", gpuLoadStatus)).c_str());
    const PDH_STATUS gpuCollectStatus = PdhCollectQueryData(impl_->gpuQuery_);
    impl_->trace_.Write(("telemetry:pdh_collect gpu_query " + tracing::Trace::FormatPdhStatus("status", gpuCollectStatus)).c_str());

    const PDH_STATUS gpuMemoryQueryStatus = PdhOpenQueryW(nullptr, 0, &impl_->gpuMemoryQuery_);
    impl_->trace_.Write(("telemetry:pdh_open gpu_memory_query " + tracing::Trace::FormatPdhStatus("status", gpuMemoryQueryStatus)).c_str());
    const PDH_STATUS gpuMemoryCounterStatus = AddCounterCompat(
        impl_->gpuMemoryQuery_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", &impl_->gpuDedicatedCounter_);
    impl_->trace_.Write(("telemetry:pdh_add gpu_memory path=\"\\\\GPU Adapter Memory(*)\\\\Dedicated Usage\" " +
        tracing::Trace::FormatPdhStatus("status", gpuMemoryCounterStatus)).c_str());
    const PDH_STATUS gpuMemoryCollectStatus = PdhCollectQueryData(impl_->gpuMemoryQuery_);
    impl_->trace_.Write(("telemetry:pdh_collect gpu_memory_query " + tracing::Trace::FormatPdhStatus("status", gpuMemoryCollectStatus)).c_str());

    const PDH_STATUS storageQueryStatus = PdhOpenQueryW(nullptr, 0, &impl_->storageQuery_);
    impl_->trace_.Write(("telemetry:pdh_open storage_query " + tracing::Trace::FormatPdhStatus("status", storageQueryStatus)).c_str());
    const PDH_STATUS storageReadStatus = AddCounterCompat(
        impl_->storageQuery_, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", &impl_->storageReadCounter_);
    impl_->trace_.Write(("telemetry:pdh_add storage_read path=\"\\\\PhysicalDisk(_Total)\\\\Disk Read Bytes/sec\" " +
        tracing::Trace::FormatPdhStatus("status", storageReadStatus)).c_str());
    const PDH_STATUS storageWriteStatus = AddCounterCompat(
        impl_->storageQuery_, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", &impl_->storageWriteCounter_);
    impl_->trace_.Write(("telemetry:pdh_add storage_write path=\"\\\\PhysicalDisk(_Total)\\\\Disk Write Bytes/sec\" " +
        tracing::Trace::FormatPdhStatus("status", storageWriteStatus)).c_str());
    const PDH_STATUS storageCollectStatus = PdhCollectQueryData(impl_->storageQuery_);
    impl_->trace_.Write(("telemetry:pdh_collect storage_query " + tracing::Trace::FormatPdhStatus("status", storageCollectStatus)).c_str());

    impl_->EnumerateDrives();
    impl_->InitializeGpuAdapterInfo();
    impl_->UpdateNetworkState(true);
    impl_->UpdateStorageThroughput(true);
    impl_->UpdateMemory();
    impl_->UpdateCpu();
    impl_->UpdateGpu();
    GetLocalTime(&impl_->snapshot_.now);
    impl_->trace_.Write("telemetry:initialize_done");
    return true;
}

void TelemetryCollector::Impl::Trace(const char* text) const {
    trace_.Write(text);
}

void TelemetryCollector::Impl::Trace(const std::string& text) const {
    trace_.Write(text);
}

const SystemSnapshot& TelemetryCollector::Snapshot() const {
    return impl_->snapshot_;
}

TelemetryDump TelemetryCollector::Dump() const {
    TelemetryDump dump;
    dump.snapshot = impl_->snapshot_;
    dump.gpuProvider.providerName = impl_->gpuProviderName_;
    dump.gpuProvider.diagnostics = impl_->gpuProviderDiagnostics_;
    dump.gpuProvider.available = impl_->gpuProviderAvailable_;
    dump.boardProvider = impl_->boardProviderSample_;
    dump.boardProvider.providerName = impl_->boardProviderName_;
    dump.boardProvider.diagnostics = impl_->boardProviderDiagnostics_;
    dump.boardProvider.available = impl_->boardProviderAvailable_;
    return dump;
}

AppConfig TelemetryCollector::EffectiveConfig() const {
    AppConfig config = impl_->config_;
    if (!impl_->snapshot_.network.adapterName.empty() && impl_->snapshot_.network.adapterName != "Auto") {
        config.networkAdapter = impl_->snapshot_.network.adapterName;
    }
    return config;
}

void TelemetryCollector::UpdateSnapshot() {
    impl_->trace_.Write("telemetry:update_snapshot_begin");
    const auto now = std::chrono::steady_clock::now();
    if (now - impl_->lastFast_ >= std::chrono::milliseconds(750)) {
        impl_->UpdateCpu();
        impl_->UpdateGpu();
        impl_->lastFast_ = now;
    }
    if (now - impl_->lastNetwork_ >= std::chrono::milliseconds(500)) {
        impl_->UpdateNetworkState(false);
        impl_->UpdateStorageThroughput(false);
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
    impl_->trace_.Write("telemetry:update_snapshot_done");
}

void TelemetryCollector::WriteDump(std::ostream& output) const {
    WriteTelemetryDump(output, Dump());
}

void TelemetryCollector::Impl::UpdateCpu() {
    if (cpuQuery_ == nullptr) {
        Trace("telemetry:cpu_update skipped=no_query");
        return;
    }
    const PDH_STATUS collectStatus = PdhCollectQueryData(cpuQuery_);
        Trace(("telemetry:cpu_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS loadStatus = PDH_INVALID_DATA;
    if (cpuLoadCounter_ != nullptr &&
        (loadStatus = PdhGetFormattedCounterValue(cpuLoadCounter_, PDH_FMT_DOUBLE, nullptr, &value)) == ERROR_SUCCESS) {
        snapshot_.cpu.loadPercent = std::clamp(value.doubleValue, 0.0, 100.0);
    }
    Trace(("telemetry:cpu_load " + tracing::Trace::FormatPdhStatus("status", loadStatus) + " " +
        tracing::Trace::FormatValueDouble("value", snapshot_.cpu.loadPercent, 2)).c_str());
    PDH_STATUS clockStatus = PDH_INVALID_DATA;
    if (cpuFrequencyCounter_ != nullptr &&
        (clockStatus = PdhGetFormattedCounterValue(cpuFrequencyCounter_, PDH_FMT_DOUBLE, nullptr, &value)) == ERROR_SUCCESS) {
        snapshot_.cpu.clock.value = value.doubleValue / 1000.0;
        snapshot_.cpu.clock.unit = "GHz";
    }
    Trace(("telemetry:cpu_clock " + tracing::Trace::FormatPdhStatus("status", clockStatus) + " value=" +
        (snapshot_.cpu.clock.value.has_value() ? FormatScalarMetric(snapshot_.cpu.clock, 2) : std::string("N/A"))).c_str());
    if (boardProvider_ != nullptr) {
        ApplyBoardVendorSample(boardProvider_->Sample());
        Trace(("telemetry:board_vendor_sample provider=" + boardProviderName_ +
            " available=" + tracing::Trace::BoolText(boardProviderAvailable_) +
            " diagnostics=\"" + boardProviderDiagnostics_ + "\"").c_str());
    }
}

void TelemetryCollector::Impl::InitializeGpuAdapterInfo() {
    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryHr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(factoryHr) || factory == nullptr) {
        char buffer[128];
        sprintf_s(buffer, "telemetry:gpu_adapter_factory hr=0x%08X", static_cast<unsigned int>(factoryHr));
        Trace(buffer);
        return;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT enumHr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (enumHr == DXGI_ERROR_NOT_FOUND) {
            Trace("telemetry:gpu_adapter_enum done");
            break;
        }
        if (FAILED(enumHr) || adapter == nullptr) {
            char buffer[128];
            sprintf_s(buffer, "telemetry:gpu_adapter_enum index=%u hr=0x%08X", adapterIndex, static_cast<unsigned int>(enumHr));
            Trace(buffer);
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        const HRESULT descHr = adapter->GetDesc1(&desc);
        if (SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            const std::string adapterName = Utf8FromWide(desc.Description);
            snapshot_.gpu.vram.totalGb = static_cast<double>(desc.DedicatedVideoMemory) /
                (1024.0 * 1024.0 * 1024.0);
            if (snapshot_.gpu.name == "GPU" && !adapterName.empty()) {
                snapshot_.gpu.name = adapterName;
            }

            char buffer[256];
            sprintf_s(buffer,
                "telemetry:gpu_adapter_selected index=%u hr=0x%08X dedicated_bytes=%llu dedicated_gb=%.2f name=\"%s\"",
                adapterIndex,
                static_cast<unsigned int>(descHr),
                static_cast<unsigned long long>(desc.DedicatedVideoMemory),
                snapshot_.gpu.vram.totalGb,
                adapterName.c_str());
            Trace(buffer);
            adapter->Release();
            break;
        }

        {
            const std::string adapterName = SUCCEEDED(descHr) ? Utf8FromWide(desc.Description) : std::string();
            char buffer[256];
            sprintf_s(buffer,
                "telemetry:gpu_adapter_skip index=%u hr=0x%08X software=%s name=\"%s\"",
                adapterIndex,
                static_cast<unsigned int>(descHr),
                tracing::Trace::BoolText(SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0).c_str(),
                adapterName.c_str());
            Trace(buffer);
        }
        adapter->Release();
    }

    factory->Release();
}

double TelemetryCollector::Impl::SumCounterArray(PDH_HCOUNTER counter, bool require3d) {
    if (counter == nullptr) {
        return 0.0;
    }
    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA) {
        Trace(("telemetry:pdh_array_prepare " + tracing::Trace::FormatPdhStatus("status", status) + " require3d=" + tracing::Trace::BoolText(require3d)).c_str());
        return 0.0;
    }
    std::vector<BYTE> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        Trace(("telemetry:pdh_array_fetch " + tracing::Trace::FormatPdhStatus("status", status) +
            " count=" + std::to_string(itemCount) + " require3d=" + tracing::Trace::BoolText(require3d)).c_str());
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
    Trace(("telemetry:pdh_array_done " + tracing::Trace::FormatPdhStatus("status", status) + " count=" +
        std::to_string(itemCount) + " require3d=" + tracing::Trace::BoolText(require3d) + " " +
        tracing::Trace::FormatValueDouble("total", total, 2)).c_str());
    return total;
}

void TelemetryCollector::Impl::ApplyGpuVendorSample(const GpuVendorTelemetrySample& sample) {
    gpuProviderName_ = sample.providerName.empty() ? "None" : sample.providerName;
    gpuProviderDiagnostics_ = sample.diagnostics.empty() ? "(none)" : sample.diagnostics;
    gpuProviderAvailable_ = sample.available;

    if (sample.name.has_value() && !sample.name->empty()) {
        snapshot_.gpu.name = *sample.name;
    }
    snapshot_.gpu.temperature.value = sample.temperatureC;
    snapshot_.gpu.clock.value = sample.coreClockMhz;
    snapshot_.gpu.clock.unit = "MHz";
    snapshot_.gpu.fan.value = sample.fanRpm;
    if (sample.totalVramGb.has_value() && *sample.totalVramGb > 0.0) {
        snapshot_.gpu.vram.totalGb = *sample.totalVramGb;
    }
}

void TelemetryCollector::Impl::ApplyBoardVendorSample(const BoardVendorTelemetrySample& sample) {
    boardProviderSample_ = sample;
    boardProviderName_ = sample.providerName.empty() ? "None" : sample.providerName;
    boardProviderDiagnostics_ = sample.diagnostics.empty() ? "(none)" : sample.diagnostics;
    boardProviderAvailable_ = sample.available;
    snapshot_.boardTemperatures = sample.temperatures;
    snapshot_.boardFans = sample.fans;
}

void TelemetryCollector::Impl::UpdateGpu() {
    if (gpuQuery_ != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(gpuQuery_);
        Trace(("telemetry:gpu_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());
        const double load3d = SumCounterArray(gpuLoadCounter_, true);
        const double loadAll = SumCounterArray(gpuLoadCounter_, false);
        snapshot_.gpu.loadPercent = std::clamp(load3d > 0.0 ? load3d : loadAll, 0.0, 100.0);
        Trace(("telemetry:gpu_load load3d=" + tracing::Trace::FormatValueDouble("value", load3d, 2) +
            " loadAll=" + tracing::Trace::FormatValueDouble("value", loadAll, 2) +
            " selected=" + tracing::Trace::FormatValueDouble("value", snapshot_.gpu.loadPercent, 2)).c_str());
    }
    if (gpuMemoryQuery_ != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(gpuMemoryQuery_);
        Trace(("telemetry:gpu_memory_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());
        const double bytes = SumCounterArray(gpuDedicatedCounter_, false);
        snapshot_.gpu.vram.usedGb = bytes / (1024.0 * 1024.0 * 1024.0);
        Trace(("telemetry:gpu_memory bytes=" + tracing::Trace::FormatValueDouble("value", bytes, 0) +
            " used_gb=" + tracing::Trace::FormatValueDouble("value", snapshot_.gpu.vram.usedGb, 2)).c_str());
    }
    if (gpuProvider_ != nullptr) {
        ApplyGpuVendorSample(gpuProvider_->Sample());
        Trace(("telemetry:gpu_vendor_sample provider=" + gpuProviderName_ +
            " available=" + tracing::Trace::BoolText(gpuProviderAvailable_) +
            " diagnostics=\"" + gpuProviderDiagnostics_ + "\"").c_str());
    }
}

void TelemetryCollector::Impl::UpdateMemory() {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    const BOOL ok = GlobalMemoryStatusEx(&memory);
    if (ok) {
        snapshot_.cpu.memory.totalGb = memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        snapshot_.cpu.memory.usedGb =
            (memory.ullTotalPhys - memory.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
    Trace(("telemetry:memory_status ok=" + tracing::Trace::BoolText(ok != FALSE) +
        " total_gb=" + tracing::Trace::FormatValueDouble("value", snapshot_.cpu.memory.totalGb, 2) +
        " used_gb=" + tracing::Trace::FormatValueDouble("value", snapshot_.cpu.memory.usedGb, 2)).c_str());
}

void TelemetryCollector::Impl::EnumerateDrives() {
    for (const auto& drive : config_.driveLetters) {
        if (!drive.empty()) {
            snapshot_.drives.push_back(DriveInfo{drive.substr(0, 1) + ":"});
            Trace(("telemetry:drive_config label=" + drive.substr(0, 1) + ":").c_str());
        }
    }
    Trace(("telemetry:drive_enumerate count=" + std::to_string(snapshot_.drives.size())).c_str());
    RefreshDriveUsage();
}

void TelemetryCollector::Impl::UpdateStorageThroughput(bool initializeOnly) {
    if (storageQuery_ == nullptr) {
        Trace("telemetry:storage_rates skipped=no_query");
        return;
    }

    const PDH_STATUS collectStatus = PdhCollectQueryData(storageQuery_);
    Trace(("telemetry:storage_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS readStatus = PDH_INVALID_DATA;
    PDH_STATUS writeStatus = PDH_INVALID_DATA;

    if (storageReadCounter_ != nullptr &&
        (readStatus = PdhGetFormattedCounterValue(storageReadCounter_, PDH_FMT_DOUBLE, nullptr, &value)) == ERROR_SUCCESS) {
        snapshot_.storage.readMbps = std::max(0.0, value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        snapshot_.storage.readMbps = 0.0;
    }

    if (storageWriteCounter_ != nullptr &&
        (writeStatus = PdhGetFormattedCounterValue(storageWriteCounter_, PDH_FMT_DOUBLE, nullptr, &value)) == ERROR_SUCCESS) {
        snapshot_.storage.writeMbps = std::max(0.0, value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        snapshot_.storage.writeMbps = 0.0;
    }

    if (!initializeOnly) {
        PushHistory(snapshot_.storage.readHistory, snapshot_.storage.readMbps);
        PushHistory(snapshot_.storage.writeHistory, snapshot_.storage.writeMbps);
    }

    Trace(("telemetry:storage_rates " +
        tracing::Trace::FormatPdhStatus("read_status", readStatus) + " " +
        tracing::Trace::FormatPdhStatus("write_status", writeStatus) +
        " read_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.storage.readMbps, 3) +
        " write_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.storage.writeMbps, 3)).c_str());
}

void TelemetryCollector::Impl::RefreshDriveUsage() {
    for (auto& drive : snapshot_.drives) {
        const std::wstring root = WideFromUtf8(drive.label + "\\");
        const UINT driveType = GetDriveTypeW(root.c_str());
        if (driveType != DRIVE_FIXED) {
            Trace(("telemetry:drive_skip label=" + drive.label + " type=" + std::to_string(driveType)).c_str());
            continue;
        }

        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        const BOOL diskOk = GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, nullptr);
        if (!diskOk || totalBytes.QuadPart == 0) {
            Trace(("telemetry:drive_space label=" + drive.label + " ok=" + tracing::Trace::BoolText(diskOk != FALSE) +
                " total_bytes=" + std::to_string(totalBytes.QuadPart)).c_str());
            continue;
        }

        const double totalGb = totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        const double freeGb = freeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        drive.freeGb = freeGb;
        drive.usedPercent = std::clamp((1.0 - (freeGb / totalGb)) * 100.0, 0.0, 100.0);
        Trace(("telemetry:drive_space label=" + drive.label +
            " total_bytes=" + std::to_string(totalBytes.QuadPart) +
            " free_bytes=" + std::to_string(freeBytes.QuadPart) +
            " used_percent=" + tracing::Trace::FormatValueDouble("value", drive.usedPercent, 1) +
            " free_gb=" + tracing::Trace::FormatValueDouble("value", drive.freeGb, 1)).c_str());
    }
}

void TelemetryCollector::Impl::PushHistory(std::vector<double>& history, double value) {
    if (history.empty()) {
        return;
    }
    history.erase(history.begin());
    history.push_back(value);
}

void TelemetryCollector::Impl::UpdateNetworkState(bool initializeOnly) {
    PMIB_IF_TABLE2 table = nullptr;
    const DWORD tableStatus = GetIfTable2(&table);
    if (tableStatus != NO_ERROR || table == nullptr) {
        Trace(("telemetry:network_table " + tracing::Trace::FormatWin32Status("status", tableStatus) +
            " table=" + tracing::Trace::BoolText(table != nullptr)).c_str());
        return;
    }
    Trace(("telemetry:network_table " + tracing::Trace::FormatWin32Status("status", tableStatus) +
        " entries=" + std::to_string(table->NumEntries) +
        " initialize_only=" + tracing::Trace::BoolText(initializeOnly)).c_str());

    ULONG addressBufferSize = 0;
    const ULONG addressProbeStatus = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &addressBufferSize);
    Trace(("telemetry:network_ip_probe " + tracing::Trace::FormatWin32Status("status", addressProbeStatus) +
        " size=" + std::to_string(addressBufferSize)).c_str());

    std::vector<BYTE> addressBuffer;
    IP_ADAPTER_ADDRESSES* addresses = nullptr;
    ULONG addressFetchStatus = addressProbeStatus;
    if (addressProbeStatus == ERROR_BUFFER_OVERFLOW && addressBufferSize > 0) {
        addressBuffer.resize(addressBufferSize);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(addressBuffer.data());
        addressFetchStatus = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, addresses, &addressBufferSize);
    }
    Trace(("telemetry:network_ip_fetch " + tracing::Trace::FormatWin32Status("status", addressFetchStatus) +
        " size=" + std::to_string(addressBufferSize)).c_str());
    if (addressFetchStatus != NO_ERROR) {
        addresses = nullptr;
    }

    const auto now = std::chrono::steady_clock::now();
    MIB_IF_ROW2* selected = nullptr;
    uint64_t selectedTraffic = 0;
    AdapterSelectionInfo selectedInfo;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        auto& row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) {
            continue;
        }

        const bool configuredExactMatch =
            !config_.networkAdapter.empty() &&
            (EqualsInsensitive(row.Alias, config_.networkAdapter) ||
                EqualsInsensitive(row.Description, config_.networkAdapter));
        const bool configuredPartialMatch =
            !config_.networkAdapter.empty() &&
            !configuredExactMatch &&
            (ContainsInsensitive(row.Alias, config_.networkAdapter) ||
                ContainsInsensitive(row.Description, config_.networkAdapter));
        if (!config_.networkAdapter.empty() && !configuredExactMatch && !configuredPartialMatch) {
            continue;
        }

        const AdapterSelectionInfo info = BuildAdapterSelectionInfo(row, addresses);
        const uint64_t traffic = row.InOctets + row.OutOctets;
        const bool hardwareInterface = row.InterfaceAndOperStatusFlags.HardwareInterface != FALSE;
        const bool connectorPresent = row.InterfaceAndOperStatusFlags.ConnectorPresent != FALSE;

        Trace(("telemetry:network_candidate interface=" + std::to_string(row.InterfaceIndex) +
            " alias=\"" + Utf8FromWide(row.Alias) + "\" description=\"" + Utf8FromWide(row.Description) + "\"" +
            " exact_match=" + tracing::Trace::BoolText(configuredExactMatch) +
            " partial_match=" + tracing::Trace::BoolText(configuredPartialMatch) +
            " matched=" + tracing::Trace::BoolText(info.matched) +
            " has_ipv4=" + tracing::Trace::BoolText(info.hasIpv4) +
            " has_gateway=" + tracing::Trace::BoolText(info.hasGateway) +
            " hardware=" + tracing::Trace::BoolText(hardwareInterface) +
            " connector=" + tracing::Trace::BoolText(connectorPresent) +
            " traffic=" + std::to_string(traffic) +
            " ip=" + info.ipAddress).c_str());

        if (config_.networkAdapter.empty()) {
            const bool candidatePreferred =
                selected == nullptr ||
                (info.hasGateway && !selectedInfo.hasGateway) ||
                (info.hasGateway == selectedInfo.hasGateway && info.hasIpv4 && !selectedInfo.hasIpv4) ||
                (info.hasGateway == selectedInfo.hasGateway && info.hasIpv4 == selectedInfo.hasIpv4 &&
                    hardwareInterface && !selected->InterfaceAndOperStatusFlags.HardwareInterface) ||
                (info.hasGateway == selectedInfo.hasGateway && info.hasIpv4 == selectedInfo.hasIpv4 &&
                    hardwareInterface == (selected->InterfaceAndOperStatusFlags.HardwareInterface != FALSE) &&
                    connectorPresent && !selected->InterfaceAndOperStatusFlags.ConnectorPresent) ||
                (info.hasGateway == selectedInfo.hasGateway && info.hasIpv4 == selectedInfo.hasIpv4 &&
                    hardwareInterface == (selected->InterfaceAndOperStatusFlags.HardwareInterface != FALSE) &&
                    connectorPresent == (selected->InterfaceAndOperStatusFlags.ConnectorPresent != FALSE) &&
                    traffic > selectedTraffic);
            if (candidatePreferred) {
                selected = &row;
                selectedTraffic = traffic;
                selectedInfo = info;
            }
        } else if (
            selected == nullptr ||
            (configuredExactMatch && !(
                EqualsInsensitive(selected->Alias, config_.networkAdapter) ||
                EqualsInsensitive(selected->Description, config_.networkAdapter))) ||
            (configuredExactMatch ==
                (EqualsInsensitive(selected->Alias, config_.networkAdapter) ||
                    EqualsInsensitive(selected->Description, config_.networkAdapter)) &&
                (info.hasGateway || info.hasIpv4))) {
            selected = &row;
            selectedTraffic = traffic;
            selectedInfo = info;
            if (configuredExactMatch && (info.hasGateway || info.hasIpv4)) {
                break;
            }
        }
    }

    if (selected != nullptr) {
        Trace(("telemetry:network_selected interface=" + std::to_string(selected->InterfaceIndex) +
            " alias=\"" + Utf8FromWide(selected->Alias) + "\" description=\"" + Utf8FromWide(selected->Description) +
            "\" has_ipv4=" + tracing::Trace::BoolText(selectedInfo.hasIpv4) +
            " has_gateway=" + tracing::Trace::BoolText(selectedInfo.hasGateway) +
            " traffic=" + std::to_string(selectedTraffic) +
            " ip=" + selectedInfo.ipAddress).c_str());
        snapshot_.network.adapterName = Utf8FromWide(
            selected->Alias[0] != L'\0' ? std::wstring_view(selected->Alias) : std::wstring_view(selected->Description));
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
                Trace(("telemetry:network_rates interface=" + std::to_string(selected->InterfaceIndex) +
                    " seconds=" + tracing::Trace::FormatValueDouble("value", seconds, 3) +
                    " upload_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.network.uploadMbps, 3) +
                    " download_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.network.downloadMbps, 3)).c_str());
            }
            previousInOctets_ = selected->InOctets;
            previousOutOctets_ = selected->OutOctets;
            previousNetworkTick_ = now;
        }

        snapshot_.network.ipAddress = selectedInfo.ipAddress;
        if (selectedInfo.hasIpv4) {
            Trace(("telemetry:network_ip_found interface=" + std::to_string(selected->InterfaceIndex) +
                " ip=" + selectedInfo.ipAddress).c_str());
        } else {
            Trace(("telemetry:network_ip_missing interface=" + std::to_string(selected->InterfaceIndex)).c_str());
        }
    } else {
        Trace("telemetry:network_selected interface=none");
    }

    FreeMibTable(table);
    Trace("telemetry:network_table_free status=done");
}
