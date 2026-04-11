#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "telemetry.h"

struct RuntimeConfigView {
    AppConfig effectiveConfig{};

    AppConfig ComposeEffectiveConfig(
        const AppConfig& telemetryConfig, const std::string& resolvedNetworkAdapterName, const std::vector<StorageDriveCandidate>& storageDrives) const;
    void SetEffectiveConfig(const AppConfig& config);
    void SetPreferredNetworkAdapterName(const std::string& adapterName);
    void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters);
};

struct RuntimeCandidateView {
    std::vector<NetworkAdapterCandidate> networkAdapters;
    std::vector<StorageDriveCandidate> storageDrives;

    void SyncFromTelemetry(const TelemetryCollector& telemetry);
    void SyncFromSnapshotAndConfig(const SystemSnapshot& snapshot, const AppConfig& config);
};
