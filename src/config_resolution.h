#pragma once

#include <string>
#include <vector>

#include "config.h"

struct LayoutBindingSelection {
    std::vector<std::string> boardTemperatureNames;
    std::vector<std::string> boardFanNames;
};

LayoutBindingSelection CollectLayoutBindings(const LayoutConfig& layout);
std::vector<std::string> NormalizeConfiguredDrives(const std::vector<std::string>& drives);
bool SelectResolvedLayout(AppConfig& config, const std::string& requestedName);
