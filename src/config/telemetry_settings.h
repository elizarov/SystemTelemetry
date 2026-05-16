#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct BoardTelemetrySettings {
    std::vector<std::string> requestedTemperatureNames;
    std::vector<std::string> requestedFanNames;
    std::unordered_map<std::string, std::string> temperatureSensorNames;
    std::unordered_map<std::string, std::string> fanSensorNames;

    bool operator==(const BoardTelemetrySettings& other) const = default;
};

struct TelemetrySelectionSettings {
    std::string preferredAdapterName;
    std::string preferredGpuAdapterName;
    std::vector<std::string> configuredDrives;

    bool operator==(const TelemetrySelectionSettings& other) const = default;
};

struct TelemetrySettings {
    BoardTelemetrySettings board;
    TelemetrySelectionSettings selection;

    bool operator==(const TelemetrySettings& other) const = default;
};

struct ResolvedTelemetrySelections {
    std::string adapterName;
    std::string gpuAdapterName;
    std::vector<std::string> drives;
    std::unordered_map<std::string, std::string> boardTemperatureSensorNames;
    std::unordered_map<std::string, std::string> boardFanSensorNames;

    bool operator==(const ResolvedTelemetrySelections& other) const = default;
};
