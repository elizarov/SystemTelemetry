#include "telemetry_runtime.h"

#include "telemetry_runtime_state.h"

namespace {

AppConfig BuildUiOverlayConfigFromResolvedTelemetry(const AppConfig& uiConfig, const TelemetryCollector& telemetry) {
    AppConfig config = telemetry.EffectiveConfig();
    config.display = uiConfig.display;
    config.layouts = uiConfig.layouts;
    config.layout = uiConfig.layout;
    return config;
}

}  // namespace

bool ShouldShowRuntimeDialogs(const DiagnosticsOptions& options) {
    return !options.trace;
}

class RealTelemetryRuntime : public TelemetryRuntime {
public:
    bool Initialize(const AppConfig& config, std::ostream* traceStream) override {
        effectiveConfig_ = config;
        if (!telemetry_.Initialize(config, traceStream)) {
            return false;
        }
        candidateView_.SyncNetworkFromTelemetry(telemetry_);
        candidateView_.SyncStorageFromTelemetry(telemetry_);
        return true;
    }

    const SystemSnapshot& Snapshot() const override {
        return telemetry_.Snapshot();
    }

    TelemetryDump Dump() const override {
        return telemetry_.Dump();
    }

    AppConfig EffectiveConfig() const override {
        return BuildUiOverlayConfigFromResolvedTelemetry(effectiveConfig_, telemetry_);
    }

    const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const override {
        return candidateView_.networkAdapters;
    }

    const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const override {
        return candidateView_.storageDrives;
    }

    void SetEffectiveConfig(const AppConfig& config) override {
        effectiveConfig_ = config;
    }

    void SetPreferredNetworkAdapterName(const std::string& adapterName) override {
        effectiveConfig_.network.adapterName = adapterName;
        telemetry_.SetPreferredNetworkAdapterName(adapterName);
        candidateView_.SyncNetworkFromTelemetry(telemetry_);
    }

    void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) override {
        effectiveConfig_.storage.drives = driveLetters;
        telemetry_.SetSelectedStorageDrives(driveLetters);
        candidateView_.SyncStorageFromTelemetry(telemetry_);
    }

    void RefreshSelectionsAndSnapshot() override {
        telemetry_.RefreshSelections();
        telemetry_.UpdateSnapshot();
        candidateView_.SyncNetworkFromTelemetry(telemetry_);
        candidateView_.SyncStorageFromTelemetry(telemetry_);
    }

    void UpdateSnapshot() override {
        telemetry_.UpdateSnapshot();
    }

private:
    TelemetryCollector telemetry_;
    AppConfig effectiveConfig_{};
    RuntimeCandidateView candidateView_{};
};

std::unique_ptr<TelemetryRuntime> CreateRealTelemetryRuntime() {
    return std::make_unique<RealTelemetryRuntime>();
}
