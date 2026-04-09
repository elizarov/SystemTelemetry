#include "telemetry_runtime.h"

#include "telemetry_runtime_state.h"
#include "trace.h"

bool ShouldShowRuntimeDialogs(const DiagnosticsOptions& options) {
    return !options.trace;
}

class RealTelemetryRuntime : public TelemetryRuntime {
public:
    bool Initialize(const AppConfig& config, std::ostream* traceStream) override {
        configView_.SetEffectiveConfig(config);
        if (!telemetry_.Initialize(config, traceStream)) {
            return false;
        }
        candidateView_.SyncFromTelemetry(telemetry_);
        return true;
    }

    const SystemSnapshot& Snapshot() const override {
        return telemetry_.Snapshot();
    }

    TelemetryDump Dump() const override {
        return telemetry_.Dump();
    }

    AppConfig EffectiveConfig() const override {
        return configView_.ComposeEffectiveConfig(telemetry_.EffectiveConfig());
    }

    const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const override {
        return candidateView_.networkAdapters;
    }

    const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const override {
        return candidateView_.storageDrives;
    }

    void SetEffectiveConfig(const AppConfig& config) override {
        configView_.SetEffectiveConfig(config);
    }

    void SetPreferredNetworkAdapterName(const std::string& adapterName) override {
        configView_.SetPreferredNetworkAdapterName(adapterName);
        telemetry_.SetPreferredNetworkAdapterName(adapterName);
        candidateView_.SyncFromTelemetry(telemetry_);
    }

    void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) override {
        configView_.SetSelectedStorageDrives(driveLetters);
        telemetry_.SetSelectedStorageDrives(driveLetters);
        candidateView_.SyncFromTelemetry(telemetry_);
    }

    void UpdateSnapshot() override {
        telemetry_.UpdateSnapshot();
        candidateView_.SyncFromTelemetry(telemetry_);
    }

private:
    TelemetryCollector telemetry_;
    RuntimeConfigView configView_{};
    RuntimeCandidateView candidateView_{};
};

std::unique_ptr<TelemetryRuntime> CreateRealTelemetryRuntime() {
    return std::make_unique<RealTelemetryRuntime>();
}
