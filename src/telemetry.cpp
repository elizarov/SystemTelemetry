#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "gpu_vendor.h"
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

std::string FormatMemoryMetric(const MemoryMetric& metric) {
    char buffer[64];
    sprintf_s(buffer, "%.1f / %.1f GB", metric.usedGb, metric.totalGb);
    return buffer;
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

}  // namespace

struct TelemetryCollector::Impl {
    ~Impl();

    void UpdateCpu();
    void UpdateGpu();
    void InitializeGpuAdapterInfo();
    void UpdateMemory();
    void ApplyGpuVendorSample(const GpuVendorTelemetrySample& sample);
    void EnumerateDrives();
    void RefreshDriveUsage();
    void UpdateNetworkState(bool initializeOnly);
    void DumpText(std::ostream& output) const;
    double SumCounterArray(PDH_HCOUNTER counter, bool require3d);
    std::string FindAdapterIp(ULONG interfaceIndex);
    static void PushHistory(std::vector<double>& history, double value);
    void Trace(const char* text) const;
    void Trace(const std::string& text) const;

    AppConfig config_;
    SystemSnapshot snapshot_;
    tracing::Trace trace_;
    std::unique_ptr<GpuVendorTelemetryProvider> gpuProvider_;
    std::string gpuProviderName_ = "None";
    std::string gpuProviderDiagnostics_ = "Provider not initialized.";
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

bool TelemetryCollector::Initialize(const AppConfig& config, std::ostream* traceStream) {
    impl_->config_ = config;
    impl_->trace_.SetOutput(traceStream);
    impl_->snapshot_.network.uploadHistory.assign(60, 0.0);
    impl_->snapshot_.network.downloadHistory.assign(60, 0.0);

    WSADATA wsaData{};
    const int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

    impl_->trace_.Write("telemetry:initialize_begin");
    {
        char buffer[128];
        sprintf_s(buffer, "telemetry:wsa_startup result=%d version=%u.%u",
            wsaStartupResult, LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
        impl_->trace_.Write(buffer);
    }
    impl_->gpuProvider_ = CreateGpuVendorTelemetryProvider(&impl_->trace_);
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

    impl_->EnumerateDrives();
    impl_->InitializeGpuAdapterInfo();
    impl_->UpdateNetworkState(true);
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

void TelemetryCollector::DumpText(std::ostream& output) const {
    impl_->DumpText(output);
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

void TelemetryCollector::Impl::DumpText(std::ostream& output) const {
    const auto now = snapshot_.now;
    char dateTime[64];
    sprintf_s(dateTime, "%04d-%02d-%02d %02d:%02d:%02d",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    output << "System Telemetry Dump\r\n";
    output << "=====================\r\n";
    output << "Timestamp: " << dateTime << "\r\n";
    output << "\r\n";

    output << "[CPU]\r\n";
    output << "Name: " << snapshot_.cpu.name << "\r\n";
    {
        char buffer[64];
        sprintf_s(buffer, "Load: %.2f%%", snapshot_.cpu.loadPercent);
        output << buffer << "\r\n";
    }
    output << "Clock: " << FormatScalarMetric(snapshot_.cpu.clock, 2) << "\r\n";
    output << "Memory: " << FormatMemoryMetric(snapshot_.cpu.memory) << "\r\n";
    output << "\r\n";

    output << "[GPU]\r\n";
    output << "Name: " << snapshot_.gpu.name << "\r\n";
    {
        char buffer[64];
        sprintf_s(buffer, "Load: %.2f%%", snapshot_.gpu.loadPercent);
        output << buffer << "\r\n";
    }
    output << "Temperature: " << FormatScalarMetric(snapshot_.gpu.temperature, 1) << "\r\n";
    output << "Clock: " << FormatScalarMetric(snapshot_.gpu.clock, 0) << "\r\n";
    output << "Fan: " << FormatScalarMetric(snapshot_.gpu.fan, 0) << "\r\n";
    output << "VRAM: " << FormatMemoryMetric(snapshot_.gpu.vram) << "\r\n";
    output << "\r\n";

    output << "[GPU Vendor Provider]\r\n";
    output << "Name: " << gpuProviderName_ << "\r\n";
    output << "Available: " << (gpuProviderAvailable_ ? "yes" : "no") << "\r\n";
    output << "Diagnostics: " << gpuProviderDiagnostics_ << "\r\n";
    output << "\r\n";

    output << "[Network]\r\n";
    output << "Adapter: " << snapshot_.network.adapterName << "\r\n";
    output << "IP: " << snapshot_.network.ipAddress << "\r\n";
    {
        char buffer[64];
        sprintf_s(buffer, "Upload: %.3f MB/s", snapshot_.network.uploadMbps);
        output << buffer << "\r\n";
        sprintf_s(buffer, "Download: %.3f MB/s", snapshot_.network.downloadMbps);
        output << buffer << "\r\n";
    }
    output << "\r\n";

    output << "[Storage]\r\n";
    if (snapshot_.drives.empty()) {
        output << "(none)\r\n";
    } else {
        for (const auto& drive : snapshot_.drives) {
            char buffer[128];
            sprintf_s(buffer, "%s used=%.1f%% free=%.1f GB", drive.label.c_str(), drive.usedPercent, drive.freeGb);
            output << buffer << "\r\n";
        }
    }
    output.flush();
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

std::string TelemetryCollector::Impl::FindAdapterIp(ULONG interfaceIndex) {
    ULONG size = 0;
    const ULONG probeStatus = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &size);
    Trace(("telemetry:network_ip_probe " + tracing::Trace::FormatWin32Status("status", probeStatus) +
        " size=" + std::to_string(size) + " interface=" + std::to_string(interfaceIndex)).c_str());
    std::vector<BYTE> buffer(size);
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    const ULONG fetchStatus = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr, addresses, &size);
    if (fetchStatus != NO_ERROR) {
        Trace(("telemetry:network_ip_fetch " + tracing::Trace::FormatWin32Status("status", fetchStatus) +
            " interface=" + std::to_string(interfaceIndex)).c_str());
        return "N/A";
    }

    for (auto* current = addresses; current != nullptr; current = current->Next) {
        if (current->IfIndex != interfaceIndex) {
            continue;
        }
        for (auto* unicast = current->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            wchar_t address[128];
            DWORD length = ARRAYSIZE(address);
            const int addressStatus = WSAAddressToStringW(
                    unicast->Address.lpSockaddr, static_cast<DWORD>(unicast->Address.iSockaddrLength),
                    nullptr, address, &length);
            if (addressStatus == 0) {
                std::wstring ip = address;
                if (ip.find(L':') == std::wstring::npos) {
                    const std::string utf8Ip = Utf8FromWide(ip);
                    Trace(("telemetry:network_ip_found interface=" + std::to_string(interfaceIndex) +
                        " ip=" + utf8Ip).c_str());
                    return utf8Ip;
                }
            } else {
                Trace(("telemetry:network_ip_stringify interface=" + std::to_string(interfaceIndex) +
                    " result=" + std::to_string(addressStatus)).c_str());
            }
        }
    }

    Trace(("telemetry:network_ip_missing interface=" + std::to_string(interfaceIndex)).c_str());
    return "N/A";
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
            Trace(("telemetry:network_selected interface=" + std::to_string(row.InterfaceIndex) +
                " alias=\"" + Utf8FromWide(row.Alias) + "\" description=\"" + Utf8FromWide(row.Description) + "\"").c_str());
            break;
        }
    }

    if (selected != nullptr) {
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

        snapshot_.network.ipAddress = FindAdapterIp(selected->InterfaceIndex);
    } else {
        Trace("telemetry:network_selected interface=none");
    }

    FreeMibTable(table);
    Trace("telemetry:network_table_free status=done");
}
