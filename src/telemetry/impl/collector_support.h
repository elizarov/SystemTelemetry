#pragma once

#include <pdh.h>
#include <string>

#include "telemetry/metric_types.h"

PDH_STATUS AddCounterCompat(PDH_HQUERY query, const wchar_t* path, PDH_HCOUNTER* counter);
std::string DetectCpuName();
std::string PdhStatusCodeString(PDH_STATUS status);
std::string FormatScalarMetric(const ScalarMetric& metric, int precision);
