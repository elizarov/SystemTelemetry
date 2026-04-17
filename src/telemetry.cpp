#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "board_vendor.h"
#include "numeric_safety.h"
#include "snapshot_dump.h"
#include "system_info_support.h"
#include "telemetry.h"
#include "telemetry/collector_cpu.h"
#include "telemetry/collector_gpu.h"
#include "telemetry/collector_network.h"
#include "telemetry/collector_state.h"
#include "telemetry/collector_storage.h"
#include "telemetry/collector_support.h"
#include "trace.h"

namespace {

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

void InitializeBoardProvider(TelemetryCollectorState& state, const BoardTelemetrySettings& settings) {
    state.boardProvider_ = CreateBoardVendorTelemetryProvider(&state.trace_);
    if (state.boardProvider_ != nullptr) {
        state.trace_.Write("telemetry:board_provider_initialize_begin");
        if (state.boardProvider_->Initialize(settings)) {
            ApplyBoardVendorSample(state, state.boardProvider_->Sample());
            state.trace_.Write("telemetry:board_provider_initialize_done provider=" + state.boardProviderName_ +
                               " available=" + tracing::Trace::BoolText(state.boardProviderAvailable_) +
                               " diagnostics=\"" + state.boardProviderDiagnostics_ + "\"");
        } else {
            ApplyBoardVendorSample(state, state.boardProvider_->Sample());
            state.trace_.Write("telemetry:board_provider_initialize_failed provider=" + state.boardProviderName_ +
                               " diagnostics=\"" + state.boardProviderDiagnostics_ + "\"");
        }
    } else {
        state.trace_.Write("telemetry:board_provider_create result=null");
    }
}

void ReconfigureBoardProvider(TelemetryCollectorState& state, const BoardTelemetrySettings& settings) {
    if (state.boardProvider_ == nullptr) {
        return;
    }

    state.trace_.Write("telemetry:board_provider_reconfigure_begin");
    if (state.boardProvider_->Initialize(settings)) {
        ApplyBoardVendorSample(state, state.boardProvider_->Sample());
        state.trace_.Write("telemetry:board_provider_reconfigure_done provider=" + state.boardProviderName_ +
                           " available=" + tracing::Trace::BoolText(state.boardProviderAvailable_) +
                           " diagnostics=\"" + state.boardProviderDiagnostics_ + "\"");
    } else {
        ApplyBoardVendorSample(state, state.boardProvider_->Sample());
        state.trace_.Write("telemetry:board_provider_reconfigure_failed provider=" + state.boardProviderName_ +
                           " diagnostics=\"" + state.boardProviderDiagnostics_ + "\"");
    }
}

void UpdateBoardMetrics(TelemetryCollectorState& state) {
    if (state.boardProvider_ != nullptr) {
        ApplyBoardVendorSample(state, state.boardProvider_->Sample());
        state.trace_.Write("telemetry:board_vendor_sample provider=" + state.boardProviderName_ + " available=" +
                           tracing::Trace::BoolText(state.boardProviderAvailable_) + " diagnostics=\"" +
                           state.boardProviderDiagnostics_ + "\"");
    }
    state.retainedHistoryStore_.PushBoardMetricSamples(state.snapshot_);
}

void InitializeStorageCollector(TelemetryCollectorState& state) {
    const PDH_STATUS queryStatus = PdhOpenQueryW(nullptr, 0, &state.storage_.query);
    state.trace_.Write(
        ("telemetry:pdh_open storage_query " + tracing::Trace::FormatPdhStatus("status", queryStatus)).c_str());
    const PDH_STATUS readStatus = AddCounterCompat(
        state.storage_.query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", &state.storage_.readCounter);
    state.trace_.Write(("telemetry:pdh_add storage_read path=\"\\\\PhysicalDisk(_Total)\\\\Disk Read Bytes/sec\" " +
                        tracing::Trace::FormatPdhStatus("status", readStatus))
                           .c_str());
    const PDH_STATUS writeStatus = AddCounterCompat(
        state.storage_.query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", &state.storage_.writeCounter);
    state.trace_.Write(("telemetry:pdh_add storage_write path=\"\\\\PhysicalDisk(_Total)\\\\Disk Write Bytes/sec\" " +
                        tracing::Trace::FormatPdhStatus("status", writeStatus))
                           .c_str());
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.storage_.query);
    state.trace_.Write(
        ("telemetry:pdh_collect storage_query " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());
}

}  // namespace

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
    InitializeBoardProvider(*state_, settings.board);
    InitializeCpuCollector(*state_);
    InitializeGpuCollector(*state_);
    InitializeStorageCollector(*state_);
    ResolveStorageSelection(*state_);
    ResolveNetworkSelection(*state_);
    CollectNetworkMetrics(*state_, true);
    CollectStorageMetrics(*state_, true);
    UpdateBoardMetrics(*state_);
    UpdateCpuMetrics(*state_);
    UpdateGpuMetrics(*state_);
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
    dump.gpuProvider.providerName = state_->gpu_.providerName;
    dump.gpuProvider.diagnostics = state_->gpu_.providerDiagnostics;
    dump.gpuProvider.available = state_->gpu_.providerAvailable;
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

    if (boardChanged) {
        ReconfigureBoardProvider(*state_, settings.board);
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
    UpdateBoardMetrics(*state_);
    UpdateCpuMetrics(*state_);
    UpdateGpuMetrics(*state_);
    CollectNetworkMetrics(*state_, false);
    CollectStorageMetrics(*state_, false);
    GetLocalTime(&state_->snapshot_.now);
    ++state_->snapshot_.revision;
    state_->trace_.Write("telemetry:update_snapshot_done");
}

void TelemetryCollector::WriteDump(std::ostream& output) const {
    WriteTelemetryDump(output, Dump());
}
