#pragma once

#include <optional>
#include <string_view>

enum class DashboardWidgetClass {
    Unknown,
    Text,
    Gauge,
    MetricList,
    Throughput,
    NetworkFooter,
    VerticalSpacer,
    VerticalSpring,
    DriveUsageList,
    ClockTime,
    ClockDate,
};

std::optional<DashboardWidgetClass> FindDashboardWidgetClass(std::string_view name);
std::string_view DashboardWidgetClassName(DashboardWidgetClass widgetClass);
