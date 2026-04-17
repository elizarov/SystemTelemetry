#include "telemetry/collector_storage_selection.h"

#include <algorithm>
#include <cctype>

bool IsSelectableStorageDriveType(UINT driveType) {
    return driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE;
}

std::string NormalizeStorageDriveLetter(const std::string& drive) {
    if (drive.empty()) {
        return {};
    }
    const unsigned char ch = static_cast<unsigned char>(drive.front());
    if (!std::isalpha(ch)) {
        return {};
    }
    return std::string(1, static_cast<char>(std::toupper(ch)));
}

std::vector<std::string> NormalizeConfiguredStorageDriveLetters(const std::vector<std::string>& drives) {
    std::vector<std::string> normalized;
    normalized.reserve(drives.size());
    for (const auto& drive : drives) {
        const std::string letter = NormalizeStorageDriveLetter(drive);
        if (letter.empty()) {
            continue;
        }
        if (std::find(normalized.begin(), normalized.end(), letter) == normalized.end()) {
            normalized.push_back(letter);
        }
    }
    std::sort(normalized.begin(), normalized.end());
    return normalized;
}

namespace {

std::vector<std::string> SelectFixedDriveLetters(const std::vector<StorageDriveCandidate>& availableDrives) {
    std::vector<std::string> drives;
    for (const auto& drive : availableDrives) {
        if (drive.driveType != DRIVE_FIXED) {
            continue;
        }
        if (std::find(drives.begin(), drives.end(), drive.letter) == drives.end()) {
            drives.push_back(drive.letter);
        }
    }
    return drives;
}

}  // namespace

std::vector<std::string> ResolveConfiguredStorageDriveLetters(
    const std::vector<std::string>& configuredDrives, const std::vector<StorageDriveCandidate>& availableDrives) {
    std::vector<std::string> resolvedDrives = NormalizeConfiguredStorageDriveLetters(configuredDrives);
    if (!resolvedDrives.empty() || !configuredDrives.empty()) {
        return resolvedDrives;
    }
    return SelectFixedDriveLetters(availableDrives);
}
