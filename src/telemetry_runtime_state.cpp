#include "telemetry_runtime_state.h"

#include "config_resolution.h"
#include "telemetry_support.h"

AppConfig RuntimeConfigView::ComposeEffectiveConfig(
    const AppConfig& telemetryConfig, const std::string& resolvedNetworkAdapterName, const std::vector<StorageDriveCandidate>& storageDrives) const {
    AppConfig config = telemetryConfig;
    config.display = effectiveConfig.display;
    config.layouts = effectiveConfig.layouts;
    config.layout = effectiveConfig.layout;
    return ResolveRuntimeSelections(config, resolvedNetworkAdapterName, storageDrives, false);
}

void RuntimeConfigView::SetEffectiveConfig(const AppConfig& config) {
    effectiveConfig = config;
}

void RuntimeConfigView::SetPreferredNetworkAdapterName(const std::string& adapterName) {
    effectiveConfig.network.adapterName = adapterName;
}

void RuntimeConfigView::SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) {
    effectiveConfig.storage.drives = driveLetters;
}

void RuntimeCandidateView::SyncFromTelemetry(const TelemetryCollector& telemetry) {
    networkAdapters = telemetry.NetworkAdapterCandidates();
    storageDrives = telemetry.StorageDriveCandidates();
}

void RuntimeCandidateView::SyncFromSnapshotAndConfig(const SystemSnapshot& snapshot, const AppConfig& config) {
    networkAdapters.clear();
    storageDrives.clear();

    if (!snapshot.network.adapterName.empty()) {
        NetworkAdapterCandidate candidate;
        candidate.adapterName = snapshot.network.adapterName;
        candidate.ipAddress = snapshot.network.ipAddress.empty() ? "N/A" : snapshot.network.ipAddress;
        candidate.selected = true;
        networkAdapters.push_back(std::move(candidate));
    }

    storageDrives.reserve(snapshot.drives.size());
    for (const auto& drive : snapshot.drives) {
        if (!IsSelectableStorageDriveType(drive.driveType)) {
            continue;
        }

        StorageDriveCandidate candidate;
        candidate.letter = NormalizeDriveLetter(drive.label);
        if (candidate.letter.empty()) {
            continue;
        }
        candidate.volumeLabel = drive.volumeLabel;
        candidate.totalGb = drive.totalGb;
        candidate.driveType = drive.driveType;
        storageDrives.push_back(std::move(candidate));
    }

    const AppConfig effectiveConfig = ResolveRuntimeSelections(config, snapshot.network.adapterName, storageDrives, false);
    for (auto& candidate : storageDrives) {
        candidate.selected =
            std::find(effectiveConfig.storage.drives.begin(), effectiveConfig.storage.drives.end(), candidate.letter) !=
            effectiveConfig.storage.drives.end();
    }

    std::sort(storageDrives.begin(),
        storageDrives.end(),
        [](const StorageDriveCandidate& lhs, const StorageDriveCandidate& rhs) { return lhs.letter < rhs.letter; });
}
