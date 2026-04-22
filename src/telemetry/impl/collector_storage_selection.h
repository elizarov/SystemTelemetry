#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "telemetry/telemetry.h"

bool IsSelectableStorageDriveType(UINT driveType);
std::string NormalizeStorageDriveLetter(const std::string& drive);
std::vector<std::string> NormalizeConfiguredStorageDriveLetters(const std::vector<std::string>& drives);
std::vector<std::string> ResolveConfiguredStorageDriveLetters(
    const std::vector<std::string>& configuredDrives, const std::vector<StorageDriveCandidate>& availableDrives);
