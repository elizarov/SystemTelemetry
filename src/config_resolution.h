#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "telemetry.h"

struct LayoutBindingSelection {
    std::vector<std::string> boardTemperatureNames;
    std::vector<std::string> boardFanNames;
};

LayoutBindingSelection CollectLayoutBindings(const LayoutConfig& layout);
std::vector<std::string> NormalizeConfiguredDrives(const std::vector<std::string>& drives);
std::vector<std::string> ResolveConfiguredDrives(
    const std::vector<std::string>& drives, const std::vector<StorageDriveCandidate>& availableDrives, bool resolveWhenEmpty = true);
AppConfig ResolveRuntimeSelections(const AppConfig& config,
    const std::string& resolvedNetworkAdapterName,
    const std::vector<StorageDriveCandidate>& availableDrives,
    bool resolveEmptyDrives);
bool SelectResolvedLayout(AppConfig& config, const std::string& requestedName);
