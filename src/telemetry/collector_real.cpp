#define NOMINMAX
#include <winsock2.h>
#include <memory>
#include <ostream>

#include "telemetry/collector_real.h"

#include "telemetry/collector_board.h"
#include "telemetry/collector_cpu.h"
#include "telemetry/collector_gpu.h"
#include "telemetry/collector_network.h"
#include "telemetry/collector_state.h"
#include "telemetry/collector_storage.h"
#include "telemetry/collector_storage_selection.h"

namespace {

class RealTelemetryCollector : public TelemetryCollector {
public:
    bool Initialize(const TelemetrySettings& settings, std::ostream* traceStream) override {
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

    const SystemSnapshot& Snapshot() const override {
        return state_->snapshot_;
    }

    TelemetryDump Dump() const override {
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

    const ResolvedTelemetrySelections& ResolvedSelections() const override {
        return state_->resolvedSelections_;
    }

    const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const override {
        return state_->network_.adapterCandidates;
    }

    const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const override {
        return state_->storage_.driveCandidates;
    }

    void ApplySettings(const TelemetrySettings& settings) override {
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

    void SetPreferredNetworkAdapterName(std::string adapterName) override {
        state_->settings_.selection.preferredAdapterName = std::move(adapterName);
        ResolveNetworkSelection(*state_);
        ++state_->snapshot_.revision;
    }

    void SetSelectedStorageDrives(std::vector<std::string> driveLetters) override {
        state_->settings_.selection.configuredDrives = NormalizeConfiguredStorageDriveLetters(driveLetters);
        ResolveStorageSelection(*state_);
        ++state_->snapshot_.revision;
    }

    void RefreshSelectionsAndSnapshot() override {
        ResolveNetworkSelection(*state_);
        ResolveStorageSelection(*state_);
        ++state_->snapshot_.revision;
        UpdateSnapshot();
    }

    void UpdateSnapshot() override {
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

private:
    std::unique_ptr<RealTelemetryCollectorState> state_ = std::make_unique<RealTelemetryCollectorState>();
};

}  // namespace

std::unique_ptr<TelemetryCollector> CreateRealTelemetryCollector() {
    return std::make_unique<RealTelemetryCollector>();
}
