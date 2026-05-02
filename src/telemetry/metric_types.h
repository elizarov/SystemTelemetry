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

#define SYSTEM_TELEMETRY_SCALAR_METRIC_ISSUE_ITEMS(X)                                                                  \
    X(None, "none")                                                                                                    \
    X(PermissionRequired, "permission_required")

ENUM_STRING_DECLARE(ScalarMetricIssue, SYSTEM_TELEMETRY_SCALAR_METRIC_ISSUE_ITEMS);

#undef SYSTEM_TELEMETRY_SCALAR_METRIC_ISSUE_ITEMS

struct ScalarMetric {
    std::optional<double> value;
    ScalarMetricUnit unit = ScalarMetricUnit::None;
    ScalarMetricIssue issue = ScalarMetricIssue::None;
};

struct NamedScalarMetric {
    std::string name;
    ScalarMetric metric;
};
