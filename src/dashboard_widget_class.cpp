#include "dashboard_widget_class.h"

std::optional<DashboardWidgetClass> FindDashboardWidgetClass(std::string_view name) {
    if (name.empty()) {
        return std::nullopt;
    }
    return EnumFromString<DashboardWidgetClass>(name);
}

std::string_view DashboardWidgetClassName(DashboardWidgetClass widgetClass) {
    return EnumToString(widgetClass);
}
