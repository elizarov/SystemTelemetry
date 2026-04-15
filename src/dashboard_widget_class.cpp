#include "dashboard_widget_class.h"

#include <array>

namespace {

struct DashboardWidgetClassMapping {
    DashboardWidgetClass widgetClass = DashboardWidgetClass::Unknown;
    std::string_view name;
};

constexpr std::array<DashboardWidgetClassMapping, 10> kDashboardWidgetClassMappings{{
    {DashboardWidgetClass::Text, "text"},
    {DashboardWidgetClass::Gauge, "gauge"},
    {DashboardWidgetClass::MetricList, "metric_list"},
    {DashboardWidgetClass::Throughput, "throughput"},
    {DashboardWidgetClass::NetworkFooter, "network_footer"},
    {DashboardWidgetClass::VerticalSpacer, "vertical_spacer"},
    {DashboardWidgetClass::VerticalSpring, "vertical_spring"},
    {DashboardWidgetClass::DriveUsageList, "drive_usage_list"},
    {DashboardWidgetClass::ClockTime, "clock_time"},
    {DashboardWidgetClass::ClockDate, "clock_date"},
}};

}  // namespace

std::optional<DashboardWidgetClass> FindDashboardWidgetClass(std::string_view name) {
    for (const auto& mapping : kDashboardWidgetClassMappings) {
        if (mapping.name == name) {
            return mapping.widgetClass;
        }
    }
    return std::nullopt;
}

std::string_view DashboardWidgetClassName(DashboardWidgetClass widgetClass) {
    for (const auto& mapping : kDashboardWidgetClassMappings) {
        if (mapping.widgetClass == widgetClass) {
            return mapping.name;
        }
    }
    return "";
}
