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

#include "snapshot_dump.h"
#include "telemetry.h"
#include "telemetry/collector_board.h"
#include "telemetry/collector_cpu.h"
#include "telemetry/collector_gpu.h"
#include "telemetry/collector_network.h"
#include "telemetry/collector_state.h"
#include "telemetry/collector_storage.h"
#include "telemetry/collector_support.h"
#include "trace.h"

TelemetryCollector::TelemetryCollector() : state_(std::make_unique<TelemetryCollectorState>()) {}

TelemetryCollector::~TelemetryCollector() = default;

TelemetryCollector::TelemetryCollector(TelemetryCollector&&) noexcept = default;

TelemetryCollector& TelemetryCollector::operator=(TelemetryCollector&&) noexcept = default;

bool TelemetryCollector::Initialize(const TelemetrySettings& settings, std::ostream* traceStream) {
    state_->settings_ = settings;
    state_->trace_.SetOutput(traceStream);
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
    InitializeBoardCollector(*state_, settings.board);
    InitializeCpuCollector(*state_);
    InitializeGpuCollector(*state_);
    InitializeStorageCollector(*state_);
    ResolveStorageSelection(*state_);
    ResolveNetworkSelection(*state_);
    UpdateNetworkMetrics(*state_, true);
    UpdateStorageMetrics(*state_, true);
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
    dump.boardProvider = state_->board_.providerSample;
    dump.boardProvider.providerName = state_->board_.providerName;
    dump.boardProvider.diagnostics = state_->board_.providerDiagnostics;
    dump.boardProvider.available = state_->board_.providerAvailable;
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
        ReconfigureBoardCollector(*state_, settings.board);
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
    UpdateNetworkMetrics(*state_, false);
    UpdateStorageMetrics(*state_, false);
    GetLocalTime(&state_->snapshot_.now);
    ++state_->snapshot_.revision;
    state_->trace_.Write("telemetry:update_snapshot_done");
}

void TelemetryCollector::WriteDump(std::ostream& output) const {
    WriteTelemetryDump(output, Dump());
}
