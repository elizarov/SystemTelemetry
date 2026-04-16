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
#include "numeric_safety.h"
#include "snapshot_dump.h"
#include "system_info_support.h"
#include "telemetry.h"
#include "telemetry/collector_state.h"
#include "telemetry/collector_network.h"
#include "telemetry/collector_storage.h"
#include "telemetry/collector_support.h"
#include "trace.h"
#include "utf8.h"

namespace {

void Trace(const TelemetryCollectorState& state, const char* text) {
    state.trace_.Write(text);
}

void Trace(const TelemetryCollectorState& state, const std::string& text) {
    state.trace_.Write(text);
}

double SumCounterArray(TelemetryCollectorState& state, PDH_HCOUNTER counter, bool require3d);
void ApplyGpuVendorSample(TelemetryCollectorState& state, const GpuVendorTelemetrySample& sample);
void ApplyBoardVendorSample(TelemetryCollectorState& state, const BoardVendorTelemetrySample& sample);
void UpdateCpu(TelemetryCollectorState& state);
void InitializeGpuAdapterInfo(TelemetryCollectorState& state);
void UpdateGpu(TelemetryCollectorState& state);
void UpdateMemory(TelemetryCollectorState& state);

}

TelemetryCollector::TelemetryCollector() : state_(std::make_unique<TelemetryCollectorState>()) {}

TelemetryCollector::~TelemetryCollector() = default;

TelemetryCollector::TelemetryCollector(TelemetryCollector&&) noexcept = default;

TelemetryCollector& TelemetryCollector::operator=(TelemetryCollector&&) noexcept = default;

bool TelemetryCollector::Initialize(const TelemetrySettings& settings, std::ostream* traceStream) {
    state_->settings_ = settings;
    state_->trace_.SetOutput(traceStream);
    state_->snapshot_.boardTemperatures =
        CreateRequestedBoardMetrics(settings.board.requestedTemperatureNames, ScalarMetricUnit::Celsius);
    state_->snapshot_.boardFans = CreateRequestedBoardMetrics(settings.board.requestedFanNames, ScalarMetricUnit::Rpm);
    state_->retainedHistoryStore_.Reset(state_->snapshot_);
    if (const std::string cpuName = DetectCpuName(); !cpuName.empty()) {
        state_->snapshot_.cpu.name = cpuName;
    }

    WSADATA wsaData{};
    const int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

    state_->trace_.Write("telemetry:initialize_begin");
    {
        char buffer[128];
        sprintf_s(buffer,
            "telemetry:wsa_startup result=%d version=%u.%u",
            wsaStartupResult,
            LOBYTE(wsaData.wVersion),
            HIBYTE(wsaData.wVersion));
        state_->trace_.Write(buffer);
    }
    state_->trace_.Write("telemetry:cpu_name value=\"" + state_->snapshot_.cpu.name + "\"");
    state_->gpuProvider_ = CreateGpuVendorTelemetryProvider(&state_->trace_);
    state_->boardProvider_ = CreateBoardVendorTelemetryProvider(&state_->trace_);
    if (state_->gpuProvider_ != nullptr) {
        state_->trace_.Write("telemetry:gpu_provider_initialize_begin");
        if (state_->gpuProvider_->Initialize()) {
            ApplyGpuVendorSample(*state_, state_->gpuProvider_->Sample());
            state_->trace_.Write("telemetry:gpu_provider_initialize_done provider=" + state_->gpuProviderName_ +
                                 " available=" + tracing::Trace::BoolText(state_->gpuProviderAvailable_) +
                                 " diagnostics=\"" + state_->gpuProviderDiagnostics_ + "\"");
        } else {
            state_->gpuProviderName_ = "AMD ADLX";
            state_->gpuProviderDiagnostics_ = "Provider initialization failed.";
            state_->trace_.Write("telemetry:gpu_provider_initialize_failed provider=" + state_->gpuProviderName_ +
                                 " diagnostics=\"" + state_->gpuProviderDiagnostics_ + "\"");
        }
    } else {
        state_->trace_.Write("telemetry:gpu_provider_create result=null");
    }
    if (state_->boardProvider_ != nullptr) {
        state_->trace_.Write("telemetry:board_provider_initialize_begin");
        if (state_->boardProvider_->Initialize(settings.board)) {
            ApplyBoardVendorSample(*state_, state_->boardProvider_->Sample());
            state_->trace_.Write("telemetry:board_provider_initialize_done provider=" + state_->boardProviderName_ +
                                 " available=" + tracing::Trace::BoolText(state_->boardProviderAvailable_) +
                                 " diagnostics=\"" + state_->boardProviderDiagnostics_ + "\"");
        } else {
            ApplyBoardVendorSample(*state_, state_->boardProvider_->Sample());
            state_->trace_.Write("telemetry:board_provider_initialize_failed provider=" + state_->boardProviderName_ +
                                 " diagnostics=\"" + state_->boardProviderDiagnostics_ + "\"");
        }
    } else {
        state_->trace_.Write("telemetry:board_provider_create result=null");
    }

    const PDH_STATUS cpuQueryStatus = PdhOpenQueryW(nullptr, 0, &state_->cpuQuery_);
    state_->trace_.Write(
        ("telemetry:pdh_open cpu_query " + tracing::Trace::FormatPdhStatus("status", cpuQueryStatus)).c_str());
    const PDH_STATUS cpuLoadStatus = AddCounterCompat(
        state_->cpuQuery_, L"\\Processor Information(_Total)\\% Processor Utility", &state_->cpuLoadCounter_);
    state_->trace_.Write(
        ("telemetry:pdh_add cpu_load path=\"\\\\Processor Information(_Total)\\\\% Processor Utility\" " +
            tracing::Trace::FormatPdhStatus("status", cpuLoadStatus))
            .c_str());
    if (state_->cpuLoadCounter_ == nullptr) {
        const PDH_STATUS cpuLoadFallbackStatus =
            AddCounterCompat(state_->cpuQuery_, L"\\Processor(_Total)\\% Processor Time", &state_->cpuLoadCounter_);
        state_->trace_.Write(
            ("telemetry:pdh_add cpu_load_fallback path=\"\\\\Processor(_Total)\\\\% Processor Time\" " +
                tracing::Trace::FormatPdhStatus("status", cpuLoadFallbackStatus))
                .c_str());
    }
    const PDH_STATUS cpuFreqStatus = AddCounterCompat(
        state_->cpuQuery_, L"\\Processor Information(_Total)\\Processor Frequency", &state_->cpuFrequencyCounter_);
    state_->trace_.Write(
        ("telemetry:pdh_add cpu_frequency path=\"\\\\Processor Information(_Total)\\\\Processor Frequency\" " +
            tracing::Trace::FormatPdhStatus("status", cpuFreqStatus))
            .c_str());
    const PDH_STATUS cpuCollectStatus = PdhCollectQueryData(state_->cpuQuery_);
    state_->trace_.Write(
        ("telemetry:pdh_collect cpu_query " + tracing::Trace::FormatPdhStatus("status", cpuCollectStatus)).c_str());

    const PDH_STATUS gpuQueryStatus = PdhOpenQueryW(nullptr, 0, &state_->gpuQuery_);
    state_->trace_.Write(
        ("telemetry:pdh_open gpu_query " + tracing::Trace::FormatPdhStatus("status", gpuQueryStatus)).c_str());
    const PDH_STATUS gpuLoadStatus =
        AddCounterCompat(state_->gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", &state_->gpuLoadCounter_);
    state_->trace_.Write(("telemetry:pdh_add gpu_load path=\"\\\\GPU Engine(*)\\\\Utilization Percentage\" " +
                          tracing::Trace::FormatPdhStatus("status", gpuLoadStatus))
                             .c_str());
    const PDH_STATUS gpuCollectStatus = PdhCollectQueryData(state_->gpuQuery_);
    state_->trace_.Write(
        ("telemetry:pdh_collect gpu_query " + tracing::Trace::FormatPdhStatus("status", gpuCollectStatus)).c_str());

    const PDH_STATUS gpuMemoryQueryStatus = PdhOpenQueryW(nullptr, 0, &state_->gpuMemoryQuery_);
    state_->trace_.Write(
        ("telemetry:pdh_open gpu_memory_query " + tracing::Trace::FormatPdhStatus("status", gpuMemoryQueryStatus))
            .c_str());
    const PDH_STATUS gpuMemoryCounterStatus = AddCounterCompat(
        state_->gpuMemoryQuery_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", &state_->gpuDedicatedCounter_);
    state_->trace_.Write(("telemetry:pdh_add gpu_memory path=\"\\\\GPU Adapter Memory(*)\\\\Dedicated Usage\" " +
                          tracing::Trace::FormatPdhStatus("status", gpuMemoryCounterStatus))
                             .c_str());
    const PDH_STATUS gpuMemoryCollectStatus = PdhCollectQueryData(state_->gpuMemoryQuery_);
    state_->trace_.Write(
        ("telemetry:pdh_collect gpu_memory_query " + tracing::Trace::FormatPdhStatus("status", gpuMemoryCollectStatus))
            .c_str());

    const PDH_STATUS storageQueryStatus = PdhOpenQueryW(nullptr, 0, &state_->storage_.query);
    state_->trace_.Write(
        ("telemetry:pdh_open storage_query " + tracing::Trace::FormatPdhStatus("status", storageQueryStatus)).c_str());
    const PDH_STATUS storageReadStatus = AddCounterCompat(
        state_->storage_.query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", &state_->storage_.readCounter);
    state_->trace_.Write(("telemetry:pdh_add storage_read path=\"\\\\PhysicalDisk(_Total)\\\\Disk Read Bytes/sec\" " +
                          tracing::Trace::FormatPdhStatus("status", storageReadStatus))
                             .c_str());
    const PDH_STATUS storageWriteStatus = AddCounterCompat(
        state_->storage_.query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", &state_->storage_.writeCounter);
    state_->trace_.Write(("telemetry:pdh_add storage_write path=\"\\\\PhysicalDisk(_Total)\\\\Disk Write Bytes/sec\" " +
                          tracing::Trace::FormatPdhStatus("status", storageWriteStatus))
                             .c_str());
    const PDH_STATUS storageCollectStatus = PdhCollectQueryData(state_->storage_.query);
    state_->trace_.Write(
        ("telemetry:pdh_collect storage_query " + tracing::Trace::FormatPdhStatus("status", storageCollectStatus))
            .c_str());

    ResolveStorageSelection(*state_);
    InitializeGpuAdapterInfo(*state_);
    ResolveNetworkSelection(*state_);
    CollectNetworkMetrics(*state_, true);
    CollectStorageMetrics(*state_, true);
    UpdateMemory(*state_);
    UpdateCpu(*state_);
    UpdateGpu(*state_);
    GetLocalTime(&state_->snapshot_.now);
    ++state_->snapshot_.revision;
    state_->trace_.Write("telemetry:initialize_done");
    return true;
}

const SystemSnapshot& TelemetryCollector::Snapshot() const {
    return state_->snapshot_;
}

TelemetryDump TelemetryCollector::Dump() const {
    TelemetryDump dump;
    dump.snapshot = state_->snapshot_;
    dump.gpuProvider.providerName = state_->gpuProviderName_;
    dump.gpuProvider.diagnostics = state_->gpuProviderDiagnostics_;
    dump.gpuProvider.available = state_->gpuProviderAvailable_;
    dump.boardProvider = state_->boardProviderSample_;
    dump.boardProvider.providerName = state_->boardProviderName_;
    dump.boardProvider.diagnostics = state_->boardProviderDiagnostics_;
    dump.boardProvider.available = state_->boardProviderAvailable_;
    return dump;
}

const ResolvedTelemetrySelections& TelemetryCollector::ResolvedSelections() const {
    return state_->resolvedSelections_;
}

const std::vector<NetworkAdapterCandidate>& TelemetryCollector::NetworkAdapterCandidates() const {
    return state_->network_.adapterCandidates;
}

const std::vector<StorageDriveCandidate>& TelemetryCollector::StorageDriveCandidates() const {
    return state_->storage_.driveCandidates;
}

void TelemetryCollector::ApplySettings(const TelemetrySettings& settings) {
    const bool boardChanged = state_->settings_.board != settings.board;
    const bool selectionChanged = state_->settings_.selection != settings.selection;
    state_->settings_ = settings;

    if (selectionChanged) {
        SetPreferredNetworkAdapterName(settings.selection.preferredAdapterName);
        SetSelectedStorageDrives(settings.selection.configuredDrives);
    }

    if (boardChanged && state_->boardProvider_ != nullptr) {
        state_->trace_.Write("telemetry:board_provider_reconfigure_begin");
        if (state_->boardProvider_->Initialize(settings.board)) {
            ApplyBoardVendorSample(*state_, state_->boardProvider_->Sample());
            state_->trace_.Write("telemetry:board_provider_reconfigure_done provider=" + state_->boardProviderName_ +
                                 " available=" + tracing::Trace::BoolText(state_->boardProviderAvailable_) +
                                 " diagnostics=\"" + state_->boardProviderDiagnostics_ + "\"");
        } else {
            ApplyBoardVendorSample(*state_, state_->boardProvider_->Sample());
            state_->trace_.Write("telemetry:board_provider_reconfigure_failed provider=" + state_->boardProviderName_ +
                                 " diagnostics=\"" + state_->boardProviderDiagnostics_ + "\"");
        }
    }
}

void TelemetryCollector::SetPreferredNetworkAdapterName(std::string adapterName) {
    state_->settings_.selection.preferredAdapterName = std::move(adapterName);
    ResolveNetworkSelection(*state_);
    ++state_->snapshot_.revision;
}

void TelemetryCollector::SetSelectedStorageDrives(std::vector<std::string> driveLetters) {
    std::vector<std::string> normalized;
    normalized.reserve(driveLetters.size());
    for (const auto& drive : driveLetters) {
        const std::string letter = NormalizeStorageDriveLetter(drive);
        if (letter.empty()) {
            continue;
        }
        if (std::find(normalized.begin(), normalized.end(), letter) == normalized.end()) {
            normalized.push_back(letter);
        }
    }
    std::sort(normalized.begin(), normalized.end());
    state_->settings_.selection.configuredDrives = std::move(normalized);
    ResolveStorageSelection(*state_);
    ++state_->snapshot_.revision;
}

void TelemetryCollector::RefreshSelections() {
    ResolveNetworkSelection(*state_);
    ResolveStorageSelection(*state_);
    ++state_->snapshot_.revision;
}

void TelemetryCollector::UpdateSnapshot() {
    state_->trace_.Write("telemetry:update_snapshot_begin");
    UpdateCpu(*state_);
    UpdateGpu(*state_);
    CollectNetworkMetrics(*state_, false);
    CollectStorageMetrics(*state_, false);
    UpdateMemory(*state_);
    GetLocalTime(&state_->snapshot_.now);
    ++state_->snapshot_.revision;
    state_->trace_.Write("telemetry:update_snapshot_done");
}

void TelemetryCollector::WriteDump(std::ostream& output) const {
    WriteTelemetryDump(output, Dump());
}

namespace {

void UpdateCpu(TelemetryCollectorState& state) {
    if (state.cpuQuery_ == nullptr) {
        Trace(state, "telemetry:cpu_update skipped=no_query");
        return;
    }
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.cpuQuery_);
    Trace(state, "telemetry:cpu_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus));

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS loadStatus = PDH_INVALID_DATA;
    if (state.cpuLoadCounter_ != nullptr &&
        (loadStatus = PdhGetFormattedCounterValue(state.cpuLoadCounter_, PDH_FMT_DOUBLE, nullptr, &value)) ==
            ERROR_SUCCESS) {
        state.snapshot_.cpu.loadPercent = ClampFinite(value.doubleValue, 0.0, 100.0);
    }
    Trace(state,
        "telemetry:cpu_load " + tracing::Trace::FormatPdhStatus("status", loadStatus) + " " +
            tracing::Trace::FormatValueDouble("value", state.snapshot_.cpu.loadPercent, 2));
    state.retainedHistoryStore_.PushSample(state.snapshot_, "cpu.load", state.snapshot_.cpu.loadPercent);
    PDH_STATUS clockStatus = PDH_INVALID_DATA;
    if (state.cpuFrequencyCounter_ != nullptr &&
        (clockStatus = PdhGetFormattedCounterValue(state.cpuFrequencyCounter_, PDH_FMT_DOUBLE, nullptr, &value)) ==
            ERROR_SUCCESS) {
        state.snapshot_.cpu.clock.value = FiniteOptional(value.doubleValue / 1000.0);
        state.snapshot_.cpu.clock.unit = ScalarMetricUnit::Gigahertz;
    }
    Trace(state,
        "telemetry:cpu_clock " + tracing::Trace::FormatPdhStatus("status", clockStatus) + " value=" +
            (state.snapshot_.cpu.clock.value.has_value() ? FormatScalarMetric(state.snapshot_.cpu.clock, 2)
                                                         : std::string("N/A")));
    if (state.boardProvider_ != nullptr) {
        ApplyBoardVendorSample(state, state.boardProvider_->Sample());
        Trace(state,
            "telemetry:board_vendor_sample provider=" + state.boardProviderName_ + " available=" +
                tracing::Trace::BoolText(state.boardProviderAvailable_) + " diagnostics=\"" +
                state.boardProviderDiagnostics_ + "\"");
    }
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, "cpu.clock", state.snapshot_.cpu.clock.value.value_or(0.0));
    state.retainedHistoryStore_.PushBoardMetricSamples(state.snapshot_);
}

void InitializeGpuAdapterInfo(TelemetryCollectorState& state) {
    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryHr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(factoryHr) || factory == nullptr) {
        char buffer[128];
        sprintf_s(buffer, "telemetry:gpu_adapter_factory hr=0x%08X", static_cast<unsigned int>(factoryHr));
        Trace(state, buffer);
        return;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT enumHr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (enumHr == DXGI_ERROR_NOT_FOUND) {
            Trace(state, "telemetry:gpu_adapter_enum done");
            break;
        }
        if (FAILED(enumHr) || adapter == nullptr) {
            char buffer[128];
            sprintf_s(buffer,
                "telemetry:gpu_adapter_enum index=%u hr=0x%08X",
                adapterIndex,
                static_cast<unsigned int>(enumHr));
            Trace(state, buffer);
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        const HRESULT descHr = adapter->GetDesc1(&desc);
        if (SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            const std::string adapterName = Utf8FromWide(desc.Description);
            state.snapshot_.gpu.vram.totalGb =
                static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0);
            if (state.snapshot_.gpu.name == "GPU" && !adapterName.empty()) {
                state.snapshot_.gpu.name = adapterName;
            }

            char buffer[256];
            sprintf_s(buffer,
                "telemetry:gpu_adapter_selected index=%u hr=0x%08X dedicated_bytes=%llu dedicated_gb=%.2f name=\"%s\"",
                adapterIndex,
                static_cast<unsigned int>(descHr),
                static_cast<unsigned long long>(desc.DedicatedVideoMemory),
                state.snapshot_.gpu.vram.totalGb,
                adapterName.c_str());
            Trace(state, buffer);
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
            Trace(state, buffer);
        }
        adapter->Release();
    }

    factory->Release();
}

double SumCounterArray(TelemetryCollectorState& state, PDH_HCOUNTER counter, bool require3d) {
    if (counter == nullptr) {
        return 0.0;
    }
    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA) {
        Trace(state,
            "telemetry:pdh_array_prepare " + tracing::Trace::FormatPdhStatus("status", status) +
                " require3d=" + tracing::Trace::BoolText(require3d));
        return 0.0;
    }
    std::vector<BYTE> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        Trace(state,
            "telemetry:pdh_array_fetch " + tracing::Trace::FormatPdhStatus("status", status) +
                " count=" + std::to_string(itemCount) + " require3d=" + tracing::Trace::BoolText(require3d));
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
        if (!IsFiniteDouble(items[i].FmtValue.doubleValue)) {
            continue;
        }
        total += items[i].FmtValue.doubleValue;
    }
    Trace(state,
        "telemetry:pdh_array_done " + tracing::Trace::FormatPdhStatus("status", status) +
            " count=" + std::to_string(itemCount) + " require3d=" + tracing::Trace::BoolText(require3d) + " " +
            tracing::Trace::FormatValueDouble("total", total, 2));
    return FiniteNonNegativeOr(total);
}

void ApplyGpuVendorSample(TelemetryCollectorState& state, const GpuVendorTelemetrySample& sample) {
    state.gpuProviderName_ = sample.providerName.empty() ? "None" : sample.providerName;
    state.gpuProviderDiagnostics_ = sample.diagnostics.empty() ? "(none)" : sample.diagnostics;
    state.gpuProviderAvailable_ = sample.available;

    if (sample.name.has_value() && !sample.name->empty()) {
        state.snapshot_.gpu.name = *sample.name;
    }
    state.snapshot_.gpu.temperature.value = FiniteOptional(sample.temperatureC);
    state.snapshot_.gpu.temperature.unit = ScalarMetricUnit::Celsius;
    state.snapshot_.gpu.clock.value = FiniteOptional(sample.coreClockMhz);
    state.snapshot_.gpu.clock.unit = ScalarMetricUnit::Megahertz;
    state.snapshot_.gpu.fan.value = FiniteOptional(sample.fanRpm);
    state.snapshot_.gpu.fan.unit = ScalarMetricUnit::Rpm;
    if (sample.totalVramGb.has_value()) {
        const double totalVramGb = FiniteNonNegativeOr(*sample.totalVramGb);
        if (totalVramGb > 0.0) {
            state.snapshot_.gpu.vram.totalGb = totalVramGb;
        }
    }
}

void ApplyBoardVendorSample(TelemetryCollectorState& state, const BoardVendorTelemetrySample& sample) {
    state.boardProviderSample_ = sample;
    state.boardProviderName_ = sample.providerName.empty() ? "None" : sample.providerName;
    state.boardProviderDiagnostics_ = sample.diagnostics.empty() ? "(none)" : sample.diagnostics;
    state.boardProviderAvailable_ = sample.available;
    state.snapshot_.boardTemperatures = sample.temperatures;
    for (auto& metric : state.snapshot_.boardTemperatures) {
        metric.metric.value = FiniteOptional(metric.metric.value);
    }
    state.snapshot_.boardFans = sample.fans;
    for (auto& metric : state.snapshot_.boardFans) {
        metric.metric.value = FiniteOptional(metric.metric.value);
    }
}

void UpdateGpu(TelemetryCollectorState& state) {
    if (state.gpuQuery_ != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpuQuery_);
        Trace(state, "telemetry:gpu_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus));
        const double load3d = SumCounterArray(state, state.gpuLoadCounter_, true);
        const double loadAll = SumCounterArray(state, state.gpuLoadCounter_, false);
        state.snapshot_.gpu.loadPercent = ClampFinite(load3d > 0.0 ? load3d : loadAll, 0.0, 100.0);
        Trace(state,
            "telemetry:gpu_load load3d=" + tracing::Trace::FormatValueDouble("value", load3d, 2) +
                " loadAll=" + tracing::Trace::FormatValueDouble("value", loadAll, 2) +
                " selected=" + tracing::Trace::FormatValueDouble("value", state.snapshot_.gpu.loadPercent, 2));
    }
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.load", state.snapshot_.gpu.loadPercent);
    if (state.gpuMemoryQuery_ != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpuMemoryQuery_);
        Trace(state, "telemetry:gpu_memory_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus));
        const double bytes = SumCounterArray(state, state.gpuDedicatedCounter_, false);
        state.snapshot_.gpu.vram.usedGb = FiniteNonNegativeOr(bytes / (1024.0 * 1024.0 * 1024.0));
        Trace(state,
            "telemetry:gpu_memory bytes=" + tracing::Trace::FormatValueDouble("value", bytes, 0) +
                " used_gb=" + tracing::Trace::FormatValueDouble("value", state.snapshot_.gpu.vram.usedGb, 2));
    }
    if (state.gpuProvider_ != nullptr) {
        ApplyGpuVendorSample(state, state.gpuProvider_->Sample());
        Trace(state,
            "telemetry:gpu_vendor_sample provider=" + state.gpuProviderName_ + " available=" +
                tracing::Trace::BoolText(state.gpuProviderAvailable_) + " diagnostics=\"" +
                state.gpuProviderDiagnostics_ + "\"");
    }
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, "gpu.temp", state.snapshot_.gpu.temperature.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, "gpu.clock", state.snapshot_.gpu.clock.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, "gpu.fan", state.snapshot_.gpu.fan.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.vram", state.snapshot_.gpu.vram.usedGb);
}

void UpdateMemory(TelemetryCollectorState& state) {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    const BOOL ok = GlobalMemoryStatusEx(&memory);
    if (ok) {
        state.snapshot_.cpu.memory.totalGb = memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        state.snapshot_.cpu.memory.usedGb =
            (memory.ullTotalPhys - memory.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
    Trace(state,
        "telemetry:memory_status ok=" + tracing::Trace::BoolText(ok != FALSE) +
            " total_gb=" + tracing::Trace::FormatValueDouble("value", state.snapshot_.cpu.memory.totalGb, 2) +
            " used_gb=" + tracing::Trace::FormatValueDouble("value", state.snapshot_.cpu.memory.usedGb, 2));
    state.retainedHistoryStore_.PushSample(state.snapshot_, "cpu.ram", state.snapshot_.cpu.memory.usedGb);
}

}  // namespace
