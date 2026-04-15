#include "telemetry_runtime.h"

namespace {
class RealTelemetryRuntime : public TelemetryRuntime {
public:
    bool Initialize(const TelemetrySettings& settings, std::ostream* traceStream) override {
        if (!telemetry_.Initialize(settings, traceStream)) {
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

    const ResolvedTelemetrySelections& ResolvedSelections() const override {
        return telemetry_.ResolvedSelections();
    }

    const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const override {
        return networkAdapters_;
    }

    const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const override {
        return storageDrives_;
    }

    void SetPreferredNetworkAdapterName(const std::string& adapterName) override {
        telemetry_.SetPreferredNetworkAdapterName(adapterName);
        networkAdapters_ = telemetry_.NetworkAdapterCandidates();
    }

    void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) override {
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
    std::vector<NetworkAdapterCandidate> networkAdapters_{};
    std::vector<StorageDriveCandidate> storageDrives_{};
};

}  // namespace

std::unique_ptr<TelemetryRuntime> CreateRealTelemetryRuntime() {
    return std::make_unique<RealTelemetryRuntime>();
}
