#include "telemetry_runtime.h"

namespace {

AppConfig BuildUiOverlayConfigFromResolvedTelemetry(const AppConfig& uiConfig, const TelemetryCollector& telemetry) {
    AppConfig config = telemetry.EffectiveConfig();
    config.display = uiConfig.display;
    config.layouts = uiConfig.layouts;
    config.layout = uiConfig.layout;
    return config;
}

}  // namespace

class RealTelemetryRuntime : public TelemetryRuntime {
public:
    bool Initialize(const AppConfig& config, std::ostream* traceStream) override {
        effectiveConfig_ = config;
        if (!telemetry_.Initialize(config, traceStream)) {
            return false;
        }
        networkAdapters_ = telemetry_.NetworkAdapterCandidates();
        storageDrives_ = telemetry_.StorageDriveCandidates();
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
        return networkAdapters_;
    }

    const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const override {
        return storageDrives_;
    }

    void SetEffectiveConfig(const AppConfig& config) override {
        effectiveConfig_ = config;
    }

    void SetPreferredNetworkAdapterName(const std::string& adapterName) override {
        effectiveConfig_.network.adapterName = adapterName;
        telemetry_.SetPreferredNetworkAdapterName(adapterName);
        networkAdapters_ = telemetry_.NetworkAdapterCandidates();
    }

    void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) override {
        effectiveConfig_.storage.drives = driveLetters;
        telemetry_.SetSelectedStorageDrives(driveLetters);
        storageDrives_ = telemetry_.StorageDriveCandidates();
    }

    void RefreshSelectionsAndSnapshot() override {
        telemetry_.RefreshSelections();
        telemetry_.UpdateSnapshot();
        networkAdapters_ = telemetry_.NetworkAdapterCandidates();
        storageDrives_ = telemetry_.StorageDriveCandidates();
    }

    void UpdateSnapshot() override {
        telemetry_.UpdateSnapshot();
    }

private:
    TelemetryCollector telemetry_;
    AppConfig effectiveConfig_{};
    std::vector<NetworkAdapterCandidate> networkAdapters_{};
    std::vector<StorageDriveCandidate> storageDrives_{};
};

std::unique_ptr<TelemetryRuntime> CreateRealTelemetryRuntime() {
    return std::make_unique<RealTelemetryRuntime>();
}
