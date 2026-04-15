#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct BoardTelemetrySettings {
    std::vector<std::string> requestedTemperatureNames;
    std::vector<std::string> requestedFanNames;
    std::unordered_map<std::string, std::string> temperatureSensorNames;
    std::unordered_map<std::string, std::string> fanSensorNames;
};

struct TelemetrySelectionSettings {
    std::string preferredAdapterName;
    std::vector<std::string> configuredDrives;
};

struct TelemetrySettings {
    BoardTelemetrySettings board;
    TelemetrySelectionSettings selection;
};

struct ResolvedTelemetrySelections {
    std::string adapterName;
    std::vector<std::string> drives;
};
