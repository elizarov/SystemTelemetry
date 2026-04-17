#pragma once

#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "telemetry.h"

bool IsSelectableStorageDriveType(UINT driveType);
std::string NormalizeStorageDriveLetter(const std::string& drive);
std::vector<std::string> NormalizeConfiguredStorageDriveLetters(const std::vector<std::string>& drives);
std::vector<std::string> ResolveConfiguredStorageDriveLetters(
    const std::vector<std::string>& configuredDrives, const std::vector<StorageDriveCandidate>& availableDrives);
