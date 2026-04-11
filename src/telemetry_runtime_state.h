#pragma once

#include <string>
#include <vector>

#include "telemetry.h"

struct RuntimeCandidateView {
    std::vector<NetworkAdapterCandidate> networkAdapters;
    std::vector<StorageDriveCandidate> storageDrives;

    void SyncNetworkFromTelemetry(const TelemetryCollector& telemetry);
    void SyncStorageFromTelemetry(const TelemetryCollector& telemetry);
    void SyncNetworkFromSnapshot(const SystemSnapshot& snapshot);
    void SyncStorageFromSnapshot(const SystemSnapshot& snapshot, const std::vector<std::string>& selectedDriveLetters);
};
