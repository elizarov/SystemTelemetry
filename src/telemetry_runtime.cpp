#include "telemetry_runtime.h"

#include "trace.h"

bool ShouldShowRuntimeDialogs(const DiagnosticsOptions& options) {
    return !options.trace;
}

class RealTelemetryRuntime : public TelemetryRuntime {
public:
    bool Initialize(const AppConfig& config, std::ostream* traceStream) override {
        effectiveConfig_ = config;
        return telemetry_.Initialize(config, traceStream);
    }

    const SystemSnapshot& Snapshot() const override {
        return telemetry_.Snapshot();
    }

    TelemetryDump Dump() const override {
        return telemetry_.Dump();
    }

    AppConfig EffectiveConfig() const override {
        AppConfig config = telemetry_.EffectiveConfig();
        config.display = effectiveConfig_.display;
        config.layouts = effectiveConfig_.layouts;
        config.layout = effectiveConfig_.layout;
        return config;
    }

    const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const override {
        return telemetry_.NetworkAdapterCandidates();
    }

    const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const override {
        return telemetry_.StorageDriveCandidates();
    }

    void SetEffectiveConfig(const AppConfig& config) override {
        effectiveConfig_ = config;
    }

    void SetPreferredNetworkAdapterName(const std::string& adapterName) override {
        effectiveConfig_.network.adapterName = adapterName;
        telemetry_.SetPreferredNetworkAdapterName(adapterName);
    }

    void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) override {
        effectiveConfig_.storage.drives = driveLetters;
        telemetry_.SetSelectedStorageDrives(driveLetters);
    }

    void UpdateSnapshot() override {
        telemetry_.UpdateSnapshot();
    }

private:
    TelemetryCollector telemetry_;
    AppConfig effectiveConfig_{};
};

std::unique_ptr<TelemetryRuntime> CreateRealTelemetryRuntime() {
    return std::make_unique<RealTelemetryRuntime>();
}
