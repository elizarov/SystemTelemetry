#include "widget.h"

#include <string_view>

#include "widget/clock_date.h"
#include "widget/clock_time.h"
#include "widget/drive_usage_list.h"
#include "widget/gauge.h"
#include "widget/metric_list.h"
#include "widget/network_footer.h"
#include "widget/vertical_spacer.h"
#include "widget/text.h"
#include "widget/throughput.h"
#include "widget/vertical_spring.h"

bool DashboardWidget::UsesFixedPreferredHeightInRows() const {
    return false;
}

bool DashboardWidget::IsHoverable() const {
    return true;
}

bool DashboardWidget::IsVerticalSpring() const {
    return false;
}

void DashboardWidget::ResolveLayoutState(const DashboardRenderer&) {}

void DashboardWidget::Draw(DashboardRenderer&, HDC, const DashboardWidgetLayout&, const DashboardMetricSource&) const {}

void DashboardWidget::FinalizeLayoutGroup(DashboardRenderer&, const std::vector<DashboardWidgetLayout*>&) {}

void DashboardWidget::BuildEditGuides(DashboardRenderer&, const DashboardWidgetLayout&) const {}

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

std::unique_ptr<DashboardWidget> CreateDashboardWidget(DashboardWidgetClass widgetClass) {
    switch (widgetClass) {
        case DashboardWidgetClass::Text:
            return std::make_unique<TextWidget>();
        case DashboardWidgetClass::Gauge:
            return std::make_unique<GaugeWidget>();
        case DashboardWidgetClass::MetricList:
            return std::make_unique<MetricListWidget>();
        case DashboardWidgetClass::Throughput:
            return std::make_unique<ThroughputWidget>();
        case DashboardWidgetClass::NetworkFooter:
            return std::make_unique<NetworkFooterWidget>();
        case DashboardWidgetClass::VerticalSpacer:
            return std::make_unique<VerticalSpacerWidget>();
        case DashboardWidgetClass::VerticalSpring:
            return std::make_unique<VerticalSpringWidget>();
        case DashboardWidgetClass::DriveUsageList:
            return std::make_unique<DriveUsageListWidget>();
        case DashboardWidgetClass::ClockTime:
            return std::make_unique<ClockTimeWidget>();
        case DashboardWidgetClass::ClockDate:
            return std::make_unique<ClockDateWidget>();
        case DashboardWidgetClass::Unknown:
        default:
            return nullptr;
    }
}

std::unique_ptr<DashboardWidget> CreateDashboardWidget(std::string_view name) {
    const auto widgetClass = FindDashboardWidgetClass(name);
    if (!widgetClass.has_value()) {
        return nullptr;
    }
    return CreateDashboardWidget(*widgetClass);
}
