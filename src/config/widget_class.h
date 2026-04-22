#pragma once

#include <optional>
#include <string_view>

#include "util/enum_string.h"

#define SYSTEM_TELEMETRY_DASHBOARD_WIDGET_CLASS_ITEMS(X)                                                               \
    X(Unknown, "")                                                                                                     \
    X(Text, "text")                                                                                                    \
    X(Gauge, "gauge")                                                                                                  \
    X(MetricList, "metric_list")                                                                                       \
    X(Throughput, "throughput")                                                                                        \
    X(NetworkFooter, "network_footer")                                                                                 \
    X(VerticalSpacer, "vertical_spacer")                                                                               \
    X(VerticalSpring, "vertical_spring")                                                                               \
    X(DriveUsageList, "drive_usage_list")                                                                              \
    X(ClockTime, "clock_time")                                                                                         \
    X(ClockDate, "clock_date")

ENUM_STRING_DECLARE(DashboardWidgetClass, SYSTEM_TELEMETRY_DASHBOARD_WIDGET_CLASS_ITEMS);

#undef SYSTEM_TELEMETRY_DASHBOARD_WIDGET_CLASS_ITEMS
