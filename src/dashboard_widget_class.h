#pragma once

#include "enum_string.h"

#include <optional>
#include <string_view>

#define SYSTEM_TELEMETRY_DASHBOARD_WIDGET_CLASS_ITEMS(X) \
    X(Unknown, "") \
    X(Text, "text") \
    X(Gauge, "gauge") \
    X(MetricList, "metric_list") \
    X(Throughput, "throughput") \
    X(NetworkFooter, "network_footer") \
    X(VerticalSpacer, "vertical_spacer") \
    X(VerticalSpring, "vertical_spring") \
    X(DriveUsageList, "drive_usage_list") \
    X(ClockTime, "clock_time") \
    X(ClockDate, "clock_date")

enum class DashboardWidgetClass {
#define SYSTEM_TELEMETRY_ENUM_ITEM(name, text) name,
    SYSTEM_TELEMETRY_DASHBOARD_WIDGET_CLASS_ITEMS(SYSTEM_TELEMETRY_ENUM_ITEM)
#undef SYSTEM_TELEMETRY_ENUM_ITEM
};

template <> struct EnumStringTraits<DashboardWidgetClass> {
    static constexpr auto values = std::to_array<DashboardWidgetClass>({
#define SYSTEM_TELEMETRY_ENUM_ITEM(name, text) DashboardWidgetClass::name,
        SYSTEM_TELEMETRY_DASHBOARD_WIDGET_CLASS_ITEMS(SYSTEM_TELEMETRY_ENUM_ITEM)
#undef SYSTEM_TELEMETRY_ENUM_ITEM
    });

    static constexpr auto names = std::to_array<std::string_view>({
#define SYSTEM_TELEMETRY_ENUM_ITEM(name, text) text,
        SYSTEM_TELEMETRY_DASHBOARD_WIDGET_CLASS_ITEMS(SYSTEM_TELEMETRY_ENUM_ITEM)
#undef SYSTEM_TELEMETRY_ENUM_ITEM
    });

    static_assert(enum_string_detail::ValidateCanonicalMappings(values, names));
};

#undef SYSTEM_TELEMETRY_DASHBOARD_WIDGET_CLASS_ITEMS

std::optional<DashboardWidgetClass> FindDashboardWidgetClass(std::string_view name);
std::string_view DashboardWidgetClassName(DashboardWidgetClass widgetClass);
