#include "telemetry/impl/system_info_support.h"

#include <algorithm>
#include <utility>

#include "util/utf8.h"

std::vector<NamedScalarMetric> CreateRequestedBoardMetrics(
    const std::vector<std::string>& names, ScalarMetricUnit unit) {
    std::vector<NamedScalarMetric> metrics;
    metrics.reserve(names.size());
    for (const auto& name : names) {
        metrics.push_back(NamedScalarMetric{name, ScalarMetric{std::nullopt, unit}});
    }
    return metrics;
}

bool HasAvailableMetricValue(const std::vector<NamedScalarMetric>& metrics) {
    for (const auto& metric : metrics) {
        if (metric.metric.value.has_value()) {
            return true;
        }
    }
    return false;
}

void UpdateDiscoveredBoardSensorNames(
    std::vector<std::string>& cachedNames, const std::vector<std::string>& latestNames) {
    if (!latestNames.empty() || cachedNames.empty()) {
        cachedNames = latestNames;
    }
}

std::vector<std::string> ExtractBoardSensorNames(const std::vector<BoardSensorReading>& readings) {
    std::vector<std::string> names;
    names.reserve(readings.size());
    for (const auto& reading : readings) {
        if (!reading.title.empty()) {
            names.push_back(reading.title);
        }
    }
    return names;
}

std::string ResolveMappedBoardSensorName(
    const std::unordered_map<std::string, std::string>& sensorNames, const std::string& logicalName) {
    const auto it = sensorNames.find(logicalName);
    if (it != sensorNames.end() && !it->second.empty()) {
        return it->second;
    }
    return logicalName;
}

void AppendRequestedBoardMetricIndex(
    BoardMetricIndexBySourceName& indexBySourceName, std::string sourceName, size_t index) {
    auto entry = std::find_if(indexBySourceName.begin(),
        indexBySourceName.end(),
        [&](const BoardMetricSourceIndexes& candidate) { return candidate.sourceName == sourceName; });
    if (entry == indexBySourceName.end()) {
        indexBySourceName.push_back(BoardMetricSourceIndexes{std::move(sourceName), {index}});
        return;
    }
    std::vector<size_t>& indices = entry->indexes;
    if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
        indices.push_back(index);
    }
}

void ResetBoardMetricValues(std::vector<NamedScalarMetric>& metrics) {
    for (auto& metric : metrics) {
        metric.metric.value.reset();
    }
}

void ApplyBoardSensorReadingsToMetrics(const std::vector<BoardSensorReading>& readings,
    const BoardMetricIndexBySourceName& indexBySourceName,
    std::vector<NamedScalarMetric>& metrics) {
    for (const auto& reading : readings) {
        const auto it = std::find_if(indexBySourceName.begin(),
            indexBySourceName.end(),
            [&](const BoardMetricSourceIndexes& candidate) { return candidate.sourceName == reading.title; });
        if (it == indexBySourceName.end()) {
            continue;
        }
        for (const size_t index : it->indexes) {
            metrics[index].metric.value = reading.value;
        }
    }
}

std::optional<std::wstring> ReadRegistryWideString(HKEY root, const char* subKey, const char* valueName) {
    const std::wstring wideSubKey = WideFromUtf8(subKey != nullptr ? std::string_view(subKey) : std::string_view());
    const std::wstring wideValueName =
        WideFromUtf8(valueName != nullptr ? std::string_view(valueName) : std::string_view());
    const wchar_t* subKeyText = subKey != nullptr ? wideSubKey.c_str() : nullptr;
    const wchar_t* valueNameText = valueName != nullptr ? wideValueName.c_str() : nullptr;
    DWORD type = 0;
    DWORD bytes = 0;
    const LONG probe = RegGetValueW(root, subKeyText, valueNameText, RRF_RT_REG_SZ, &type, nullptr, &bytes);
    if (probe != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return std::nullopt;
    }

    std::wstring value(bytes / sizeof(wchar_t), wchar_t{});
    const LONG status = RegGetValueW(root, subKeyText, valueNameText, RRF_RT_REG_SZ, &type, value.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }

    while (!value.empty() && value.back() == wchar_t{}) {
        value.pop_back();
    }
    return value;
}

std::optional<std::string> ReadRegistryString(HKEY root, const char* subKey, const char* valueName) {
    const auto value = ReadRegistryWideString(root, subKey, valueName);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return Utf8FromWide(*value);
}
