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
#include <chrono>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "board_vendor.h"
#include "gpu_vendor.h"
#include "snapshot_dump.h"
#include "telemetry.h"
#include "telemetry_internal.h"
#include "telemetry_support.h"
#include "trace.h"
#include "utf8.h"

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
    if (storage_.query != nullptr) {
        PdhCloseQuery(storage_.query);
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
    impl_->snapshot_.boardTemperatures = CreateRequestedBoardMetrics(config.board.requestedTemperatureNames,
        "\xC2\xB0"
        "C");
    impl_->snapshot_.boardFans = CreateRequestedBoardMetrics(config.board.requestedFanNames, "RPM");
    impl_->retainedHistoryStore_.Reset(impl_->snapshot_);
    if (const std::string cpuName = DetectCpuName(); !cpuName.empty()) {
        impl_->snapshot_.cpu.name = cpuName;
    }

    WSADATA wsaData{};
    const int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

    impl_->trace_.Write("telemetry:initialize_begin");
    {
        char buffer[128];
        sprintf_s(buffer,
            "telemetry:wsa_startup result=%d version=%u.%u",
            wsaStartupResult,
            LOBYTE(wsaData.wVersion),
            HIBYTE(wsaData.wVersion));
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
    impl_->trace_.Write(
        ("telemetry:pdh_open cpu_query " + tracing::Trace::FormatPdhStatus("status", cpuQueryStatus)).c_str());
    const PDH_STATUS cpuLoadStatus = AddCounterCompat(
        impl_->cpuQuery_, L"\\Processor Information(_Total)\\% Processor Utility", &impl_->cpuLoadCounter_);
    impl_->trace_.Write(
        ("telemetry:pdh_add cpu_load path=\"\\\\Processor Information(_Total)\\\\% Processor Utility\" " +
            tracing::Trace::FormatPdhStatus("status", cpuLoadStatus))
            .c_str());
    if (impl_->cpuLoadCounter_ == nullptr) {
        const PDH_STATUS cpuLoadFallbackStatus =
            AddCounterCompat(impl_->cpuQuery_, L"\\Processor(_Total)\\% Processor Time", &impl_->cpuLoadCounter_);
        impl_->trace_.Write(("telemetry:pdh_add cpu_load_fallback path=\"\\\\Processor(_Total)\\\\% Processor Time\" " +
                             tracing::Trace::FormatPdhStatus("status", cpuLoadFallbackStatus))
                .c_str());
    }
    const PDH_STATUS cpuFreqStatus = AddCounterCompat(
        impl_->cpuQuery_, L"\\Processor Information(_Total)\\Processor Frequency", &impl_->cpuFrequencyCounter_);
    impl_->trace_.Write(
        ("telemetry:pdh_add cpu_frequency path=\"\\\\Processor Information(_Total)\\\\Processor Frequency\" " +
            tracing::Trace::FormatPdhStatus("status", cpuFreqStatus))
            .c_str());
    const PDH_STATUS cpuCollectStatus = PdhCollectQueryData(impl_->cpuQuery_);
    impl_->trace_.Write(
        ("telemetry:pdh_collect cpu_query " + tracing::Trace::FormatPdhStatus("status", cpuCollectStatus)).c_str());

    const PDH_STATUS gpuQueryStatus = PdhOpenQueryW(nullptr, 0, &impl_->gpuQuery_);
    impl_->trace_.Write(
        ("telemetry:pdh_open gpu_query " + tracing::Trace::FormatPdhStatus("status", gpuQueryStatus)).c_str());
    const PDH_STATUS gpuLoadStatus =
        AddCounterCompat(impl_->gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", &impl_->gpuLoadCounter_);
    impl_->trace_.Write(("telemetry:pdh_add gpu_load path=\"\\\\GPU Engine(*)\\\\Utilization Percentage\" " +
                         tracing::Trace::FormatPdhStatus("status", gpuLoadStatus))
            .c_str());
    const PDH_STATUS gpuCollectStatus = PdhCollectQueryData(impl_->gpuQuery_);
    impl_->trace_.Write(
        ("telemetry:pdh_collect gpu_query " + tracing::Trace::FormatPdhStatus("status", gpuCollectStatus)).c_str());

    const PDH_STATUS gpuMemoryQueryStatus = PdhOpenQueryW(nullptr, 0, &impl_->gpuMemoryQuery_);
    impl_->trace_.Write(
        ("telemetry:pdh_open gpu_memory_query " + tracing::Trace::FormatPdhStatus("status", gpuMemoryQueryStatus))
            .c_str());
    const PDH_STATUS gpuMemoryCounterStatus = AddCounterCompat(
        impl_->gpuMemoryQuery_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", &impl_->gpuDedicatedCounter_);
    impl_->trace_.Write(("telemetry:pdh_add gpu_memory path=\"\\\\GPU Adapter Memory(*)\\\\Dedicated Usage\" " +
                         tracing::Trace::FormatPdhStatus("status", gpuMemoryCounterStatus))
            .c_str());
    const PDH_STATUS gpuMemoryCollectStatus = PdhCollectQueryData(impl_->gpuMemoryQuery_);
    impl_->trace_.Write(
        ("telemetry:pdh_collect gpu_memory_query " + tracing::Trace::FormatPdhStatus("status", gpuMemoryCollectStatus))
            .c_str());

    const PDH_STATUS storageQueryStatus = PdhOpenQueryW(nullptr, 0, &impl_->storage_.query);
    impl_->trace_.Write(
        ("telemetry:pdh_open storage_query " + tracing::Trace::FormatPdhStatus("status", storageQueryStatus)).c_str());
    const PDH_STATUS storageReadStatus = AddCounterCompat(
        impl_->storage_.query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", &impl_->storage_.readCounter);
    impl_->trace_.Write(("telemetry:pdh_add storage_read path=\"\\\\PhysicalDisk(_Total)\\\\Disk Read Bytes/sec\" " +
                         tracing::Trace::FormatPdhStatus("status", storageReadStatus))
            .c_str());
    const PDH_STATUS storageWriteStatus = AddCounterCompat(
        impl_->storage_.query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", &impl_->storage_.writeCounter);
    impl_->trace_.Write(("telemetry:pdh_add storage_write path=\"\\\\PhysicalDisk(_Total)\\\\Disk Write Bytes/sec\" " +
                         tracing::Trace::FormatPdhStatus("status", storageWriteStatus))
            .c_str());
    const PDH_STATUS storageCollectStatus = PdhCollectQueryData(impl_->storage_.query);
    impl_->trace_.Write(
        ("telemetry:pdh_collect storage_query " + tracing::Trace::FormatPdhStatus("status", storageCollectStatus))
            .c_str());

    impl_->RefreshStorageDriveCandidates();
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
        config.network.adapterName = impl_->snapshot_.network.adapterName;
    }
    return config;
}

const std::vector<NetworkAdapterCandidate>& TelemetryCollector::NetworkAdapterCandidates() const {
    return impl_->network_.adapterCandidates;
}

const std::vector<StorageDriveCandidate>& TelemetryCollector::StorageDriveCandidates() const {
    return impl_->storage_.driveCandidates;
}

void TelemetryCollector::SetPreferredNetworkAdapterName(std::string adapterName) {
    impl_->config_.network.adapterName = std::move(adapterName);
}

void TelemetryCollector::SetSelectedStorageDrives(std::vector<std::string> driveLetters) {
    std::vector<std::string> normalized;
    normalized.reserve(driveLetters.size());
    for (const auto& drive : driveLetters) {
        const std::string letter = NormalizeDriveLetter(drive);
        if (letter.empty()) {
            continue;
        }
        if (std::find(normalized.begin(), normalized.end(), letter) == normalized.end()) {
            normalized.push_back(letter);
        }
    }
    std::sort(normalized.begin(), normalized.end());
    impl_->config_.storage.drives = std::move(normalized);
    impl_->RefreshStorageDriveCandidates();
    impl_->EnumerateDrives();
}

void TelemetryCollector::UpdateSnapshot() {
    impl_->trace_.Write("telemetry:update_snapshot_begin");
    impl_->UpdateCpu();
    impl_->UpdateGpu();
    impl_->UpdateNetworkState(false);
    impl_->UpdateStorageThroughput(false);
    impl_->RefreshStorageDriveCandidates();
    impl_->UpdateMemory();
    impl_->RefreshDriveUsage();
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
           tracing::Trace::FormatValueDouble("value", snapshot_.cpu.loadPercent, 2))
            .c_str());
    retainedHistoryStore_.PushSample(snapshot_, "cpu.load", snapshot_.cpu.loadPercent / 100.0);
    PDH_STATUS clockStatus = PDH_INVALID_DATA;
    if (cpuFrequencyCounter_ != nullptr &&
        (clockStatus = PdhGetFormattedCounterValue(cpuFrequencyCounter_, PDH_FMT_DOUBLE, nullptr, &value)) ==
            ERROR_SUCCESS) {
        snapshot_.cpu.clock.value = value.doubleValue / 1000.0;
        snapshot_.cpu.clock.unit = "GHz";
    }
    Trace(("telemetry:cpu_clock " + tracing::Trace::FormatPdhStatus("status", clockStatus) + " value=" +
           (snapshot_.cpu.clock.value.has_value() ? FormatScalarMetric(snapshot_.cpu.clock, 2) : std::string("N/A")))
            .c_str());
    if (boardProvider_ != nullptr) {
        ApplyBoardVendorSample(boardProvider_->Sample());
        Trace(("telemetry:board_vendor_sample provider=" + boardProviderName_ + " available=" +
               tracing::Trace::BoolText(boardProviderAvailable_) + " diagnostics=\"" + boardProviderDiagnostics_ + "\"")
                .c_str());
    }
    retainedHistoryStore_.PushSample(snapshot_,
        "cpu.clock",
        config_.metricScales.cpuClockGHz > 0.0
            ? snapshot_.cpu.clock.value.value_or(0.0) / config_.metricScales.cpuClockGHz
            : 0.0);
    retainedHistoryStore_.PushBoardMetricSamples(snapshot_, config_.metricScales);
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
            sprintf_s(buffer,
                "telemetry:gpu_adapter_enum index=%u hr=0x%08X",
                adapterIndex,
                static_cast<unsigned int>(enumHr));
            Trace(buffer);
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        const HRESULT descHr = adapter->GetDesc1(&desc);
        if (SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            const std::string adapterName = Utf8FromWide(desc.Description);
            snapshot_.gpu.vram.totalGb = static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0);
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
        Trace(("telemetry:pdh_array_prepare " + tracing::Trace::FormatPdhStatus("status", status) +
               " require3d=" + tracing::Trace::BoolText(require3d))
                .c_str());
        return 0.0;
    }
    std::vector<BYTE> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        Trace(("telemetry:pdh_array_fetch " + tracing::Trace::FormatPdhStatus("status", status) +
               " count=" + std::to_string(itemCount) + " require3d=" + tracing::Trace::BoolText(require3d))
                .c_str());
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
    Trace(("telemetry:pdh_array_done " + tracing::Trace::FormatPdhStatus("status", status) +
           " count=" + std::to_string(itemCount) + " require3d=" + tracing::Trace::BoolText(require3d) + " " +
           tracing::Trace::FormatValueDouble("total", total, 2))
            .c_str());
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
               " selected=" + tracing::Trace::FormatValueDouble("value", snapshot_.gpu.loadPercent, 2))
                .c_str());
    }
    retainedHistoryStore_.PushSample(snapshot_, "gpu.load", snapshot_.gpu.loadPercent / 100.0);
    if (gpuMemoryQuery_ != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(gpuMemoryQuery_);
        Trace(("telemetry:gpu_memory_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());
        const double bytes = SumCounterArray(gpuDedicatedCounter_, false);
        snapshot_.gpu.vram.usedGb = bytes / (1024.0 * 1024.0 * 1024.0);
        Trace(("telemetry:gpu_memory bytes=" + tracing::Trace::FormatValueDouble("value", bytes, 0) +
               " used_gb=" + tracing::Trace::FormatValueDouble("value", snapshot_.gpu.vram.usedGb, 2))
                .c_str());
    }
    if (gpuProvider_ != nullptr) {
        ApplyGpuVendorSample(gpuProvider_->Sample());
        Trace(("telemetry:gpu_vendor_sample provider=" + gpuProviderName_ + " available=" +
               tracing::Trace::BoolText(gpuProviderAvailable_) + " diagnostics=\"" + gpuProviderDiagnostics_ + "\"")
                .c_str());
    }
    retainedHistoryStore_.PushSample(snapshot_,
        "gpu.temp",
        config_.metricScales.gpuTemperatureC > 0.0
            ? snapshot_.gpu.temperature.value.value_or(0.0) / config_.metricScales.gpuTemperatureC
            : 0.0);
    retainedHistoryStore_.PushSample(snapshot_,
        "gpu.clock",
        config_.metricScales.gpuClockMHz > 0.0
            ? snapshot_.gpu.clock.value.value_or(0.0) / config_.metricScales.gpuClockMHz
            : 0.0);
    retainedHistoryStore_.PushSample(snapshot_,
        "gpu.fan",
        config_.metricScales.gpuFanRpm > 0.0 ? snapshot_.gpu.fan.value.value_or(0.0) / config_.metricScales.gpuFanRpm
                                             : 0.0);
    const double totalVram = snapshot_.gpu.vram.totalGb;
    retainedHistoryStore_.PushSample(
        snapshot_, "gpu.vram", totalVram > 0.0 ? snapshot_.gpu.vram.usedGb / totalVram : 0.0);
}

void TelemetryCollector::Impl::UpdateMemory() {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    const BOOL ok = GlobalMemoryStatusEx(&memory);
    if (ok) {
        snapshot_.cpu.memory.totalGb = memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        snapshot_.cpu.memory.usedGb = (memory.ullTotalPhys - memory.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
    Trace(("telemetry:memory_status ok=" + tracing::Trace::BoolText(ok != FALSE) +
           " total_gb=" + tracing::Trace::FormatValueDouble("value", snapshot_.cpu.memory.totalGb, 2) +
           " used_gb=" + tracing::Trace::FormatValueDouble("value", snapshot_.cpu.memory.usedGb, 2))
            .c_str());
    retainedHistoryStore_.PushSample(snapshot_,
        "cpu.ram",
        snapshot_.cpu.memory.totalGb > 0.0 ? snapshot_.cpu.memory.usedGb / snapshot_.cpu.memory.totalGb : 0.0);
}
