#pragma once

#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>

#include "telemetry.h"

std::string NormalizeStorageDriveLetter(const std::string& drive);
bool IsSelectableStorageDriveType(UINT driveType);
std::vector<StorageDriveCandidate> EnumerateStorageDriveCandidates();
std::vector<StorageDriveCandidate> EnumerateSnapshotStorageDriveCandidates(const SystemSnapshot& snapshot);
std::vector<std::string> ResolveConfiguredStorageDrives(
    const std::vector<std::string>& configuredDrives, const std::vector<StorageDriveCandidate>& availableDrives);
void MarkSelectedStorageDriveCandidates(
    std::vector<StorageDriveCandidate>& candidates, const std::vector<std::string>& selectedDrives);
