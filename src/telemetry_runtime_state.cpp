#include "telemetry_runtime_state.h"

#include <algorithm>

#include "telemetry_storage_source.h"

void RuntimeCandidateView::SyncNetworkFromTelemetry(const TelemetryCollector& telemetry) {
    networkAdapters = telemetry.NetworkAdapterCandidates();
}

void RuntimeCandidateView::SyncStorageFromTelemetry(const TelemetryCollector& telemetry) {
    storageDrives = telemetry.StorageDriveCandidates();
}

void RuntimeCandidateView::SyncNetworkFromSnapshot(const SystemSnapshot& snapshot) {
    networkAdapters.clear();
    if (snapshot.network.adapterName.empty()) {
        return;
    }

    NetworkAdapterCandidate candidate;
    candidate.adapterName = snapshot.network.adapterName;
    candidate.ipAddress = snapshot.network.ipAddress.empty() ? "N/A" : snapshot.network.ipAddress;
    candidate.selected = true;
    networkAdapters.push_back(std::move(candidate));
}

void RuntimeCandidateView::SyncStorageFromSnapshot(
    const SystemSnapshot& snapshot, const std::vector<std::string>& selectedDriveLetters) {
    storageDrives.clear();

    storageDrives.reserve(snapshot.drives.size());
    for (const auto& drive : snapshot.drives) {
        if (!IsSelectableStorageDriveType(drive.driveType)) {
            continue;
        }

        StorageDriveCandidate candidate;
        candidate.letter = NormalizeStorageDriveLetter(drive.label);
        if (candidate.letter.empty()) {
            continue;
        }
        candidate.volumeLabel = drive.volumeLabel;
        candidate.totalGb = drive.totalGb;
        candidate.driveType = drive.driveType;
        candidate.selected =
            std::find(selectedDriveLetters.begin(), selectedDriveLetters.end(), candidate.letter) !=
            selectedDriveLetters.end();
        storageDrives.push_back(std::move(candidate));
    }

    std::sort(storageDrives.begin(),
        storageDrives.end(),
        [](const StorageDriveCandidate& lhs, const StorageDriveCandidate& rhs) { return lhs.letter < rhs.letter; });
}
