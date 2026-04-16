#pragma once

#define NOMINMAX
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "metric_types.h"

std::vector<NamedScalarMetric> CreateRequestedBoardMetrics(
    const std::vector<std::string>& names, ScalarMetricUnit unit);
bool HasAvailableMetricValue(const std::vector<NamedScalarMetric>& metrics);
std::optional<std::wstring> ReadRegistryWideString(HKEY root, const wchar_t* subKey, const wchar_t* valueName);
std::optional<std::string> ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName);
