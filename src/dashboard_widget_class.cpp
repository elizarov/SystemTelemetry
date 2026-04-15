#include "widget.h"

#include <string_view>

std::optional<DashboardWidgetClass> FindDashboardWidgetClass(std::string_view name) {
    if (name == "text") {
        return DashboardWidgetClass::Text;
    }
    if (name == "gauge") {
        return DashboardWidgetClass::Gauge;
    }
    if (name == "metric_list") {
        return DashboardWidgetClass::MetricList;
    }
    if (name == "throughput") {
        return DashboardWidgetClass::Throughput;
    }
    if (name == "network_footer") {
        return DashboardWidgetClass::NetworkFooter;
    }
    if (name == "vertical_spacer") {
        return DashboardWidgetClass::VerticalSpacer;
    }
    if (name == "vertical_spring") {
        return DashboardWidgetClass::VerticalSpring;
    }
    if (name == "drive_usage_list") {
        return DashboardWidgetClass::DriveUsageList;
    }
    if (name == "clock_time") {
        return DashboardWidgetClass::ClockTime;
    }
    if (name == "clock_date") {
        return DashboardWidgetClass::ClockDate;
    }
    return std::nullopt;
}

std::string_view DashboardWidgetClassName(DashboardWidgetClass widgetClass) {
    switch (widgetClass) {
        case DashboardWidgetClass::Text:
            return "text";
        case DashboardWidgetClass::Gauge:
            return "gauge";
        case DashboardWidgetClass::MetricList:
            return "metric_list";
        case DashboardWidgetClass::Throughput:
            return "throughput";
        case DashboardWidgetClass::NetworkFooter:
            return "network_footer";
        case DashboardWidgetClass::VerticalSpacer:
            return "vertical_spacer";
        case DashboardWidgetClass::VerticalSpring:
            return "vertical_spring";
        case DashboardWidgetClass::DriveUsageList:
            return "drive_usage_list";
        case DashboardWidgetClass::ClockTime:
            return "clock_time";
        case DashboardWidgetClass::ClockDate:
            return "clock_date";
        case DashboardWidgetClass::Unknown:
        default:
            return "";
    }
}
