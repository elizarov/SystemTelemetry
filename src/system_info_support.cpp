#include "system_info_support.h"

#include "utf8.h"

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

std::optional<std::wstring> ReadRegistryWideString(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD bytes = 0;
    const LONG probe = RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, nullptr, &bytes);
    if (probe != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return std::nullopt;
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    const LONG status = RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, value.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }

    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

std::optional<std::string> ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    const auto value = ReadRegistryWideString(root, subKey, valueName);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return Utf8FromWide(*value);
}
