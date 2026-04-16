#pragma once

#define NOMINMAX
#include <windows.h>
#include <pdh.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "telemetry.h"

std::string ToLowerAscii(std::string value);
std::string FormatScalarMetric(const ScalarMetric& metric, int precision);
std::vector<NamedScalarMetric> CreateRequestedBoardMetrics(
    const std::vector<std::string>& names, ScalarMetricUnit unit);
bool HasAvailableMetricValue(const std::vector<NamedScalarMetric>& metrics);
PDH_STATUS AddCounterCompat(PDH_HQUERY query, const wchar_t* path, PDH_HCOUNTER* counter);
bool ContainsInsensitive(const std::wstring& value, const std::string& needle);
bool EqualsInsensitive(const std::wstring& value, const std::string& needle);
std::string TrimAsciiWhitespace(std::string value);
std::string CollapseAsciiWhitespace(std::string value);
std::optional<std::wstring> ReadRegistryWideString(HKEY root, const wchar_t* subKey, const wchar_t* valueName);
std::optional<std::string> ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName);
std::string DetectCpuName();
