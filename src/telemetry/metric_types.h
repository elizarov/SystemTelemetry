#pragma once

#include <optional>
#include <string>

#include "util/enum_string.h"

#define SYSTEM_TELEMETRY_SCALAR_METRIC_UNIT_ITEMS(X)                                                                   \
    X(None, "")                                                                                                        \
    X(Celsius, "C")                                                                                                    \
    X(Gigahertz, "GHz")                                                                                                \
    X(Megahertz, "MHz")                                                                                                \
    X(Fps, "FPS")                                                                                                      \
    X(Rpm, "RPM")

ENUM_STRING_DECLARE(ScalarMetricUnit, SYSTEM_TELEMETRY_SCALAR_METRIC_UNIT_ITEMS);

#undef SYSTEM_TELEMETRY_SCALAR_METRIC_UNIT_ITEMS

struct ScalarMetric {
    std::optional<double> value;
    ScalarMetricUnit unit = ScalarMetricUnit::None;
};

struct NamedScalarMetric {
    std::string name;
    ScalarMetric metric;
};
