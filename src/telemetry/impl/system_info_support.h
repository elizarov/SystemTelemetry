#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "telemetry/metric_types.h"

struct BoardSensorReading {
    std::string title;
    std::optional<double> value;
};

struct BoardMetricSourceIndexes {
    std::string sourceName;
    std::vector<size_t> indexes;
};

// Size: board source maps are tiny; flat entries measured smaller than std::unordered_map.
using BoardMetricIndexBySourceName = std::vector<BoardMetricSourceIndexes>;

std::vector<NamedScalarMetric> CreateRequestedBoardMetrics(
    const std::vector<std::string>& names, ScalarMetricUnit unit);
bool HasAvailableMetricValue(const std::vector<NamedScalarMetric>& metrics);
void UpdateDiscoveredBoardSensorNames(
    std::vector<std::string>& cachedNames, const std::vector<std::string>& latestNames);
std::vector<std::string> ExtractBoardSensorNames(const std::vector<BoardSensorReading>& readings);
std::string ResolveMappedBoardSensorName(
    const std::unordered_map<std::string, std::string>& sensorNames, const std::string& logicalName);
void AppendRequestedBoardMetricIndex(
    BoardMetricIndexBySourceName& indexBySourceName, std::string sourceName, size_t index);
void ResetBoardMetricValues(std::vector<NamedScalarMetric>& metrics);
void ApplyBoardSensorReadingsToMetrics(const std::vector<BoardSensorReading>& readings,
    const BoardMetricIndexBySourceName& indexBySourceName,
    std::vector<NamedScalarMetric>& metrics);
std::optional<std::string> ReadRegistryString(HKEY root, const char* subKey, const char* valueName);
