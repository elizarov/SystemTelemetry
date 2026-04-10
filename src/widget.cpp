#include "widget.h"

#include <string_view>

#include "widget/clock_date.h"
#include "widget/clock_time.h"
#include "widget/drive_usage_list.h"
#include "widget/gauge.h"
#include "widget/metric_list.h"
#include "widget/network_footer.h"
#include "widget/spacer.h"
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

void DashboardWidget::Draw(DashboardRenderer&, HDC, const DashboardWidgetLayout&, const DashboardMetricSource&) const {}

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
    if (name == "spacer") {
        return DashboardWidgetClass::Spacer;
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

std::unique_ptr<DashboardWidget> CreateDashboardWidget(const std::string& name) {
    if (name == "text") {
        return std::make_unique<TextWidget>();
    }
    if (name == "gauge") {
        return std::make_unique<GaugeWidget>();
    }
    if (name == "metric_list") {
        return std::make_unique<MetricListWidget>();
    }
    if (name == "throughput") {
        return std::make_unique<ThroughputWidget>();
    }
    if (name == "network_footer") {
        return std::make_unique<NetworkFooterWidget>();
    }
    if (name == "spacer") {
        return std::make_unique<SpacerWidget>();
    }
    if (name == "vertical_spring") {
        return std::make_unique<VerticalSpringWidget>();
    }
    if (name == "drive_usage_list") {
        return std::make_unique<DriveUsageListWidget>();
    }
    if (name == "clock_time") {
        return std::make_unique<ClockTimeWidget>();
    }
    if (name == "clock_date") {
        return std::make_unique<ClockDateWidget>();
    }
    return nullptr;
}
