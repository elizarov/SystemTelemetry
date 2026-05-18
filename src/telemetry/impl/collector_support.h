#pragma once

#include <pdh.h>
#include <string>
#include <string_view>

#include "telemetry/metric_types.h"

PDH_STATUS AddCounterCompat(PDH_HQUERY query, std::string_view path, PDH_HCOUNTER* counter);
std::string DetectCpuName();
std::string FormatScalarMetric(const ScalarMetric& metric, int precision);
