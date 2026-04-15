#include "widget.h"

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

void DashboardWidget::ResolveLayoutState(const DashboardRenderer&, const RenderRect&) {}

void DashboardWidget::Draw(DashboardRenderer&, const DashboardWidgetLayout&, const DashboardMetricSource&) const {}

void DashboardWidget::FinalizeLayoutGroup(DashboardRenderer&, const std::vector<DashboardWidgetLayout*>&) {}

void DashboardWidget::BuildEditGuides(DashboardRenderer&, const DashboardWidgetLayout&) const {}

void DashboardWidget::BuildStaticAnchors(DashboardRenderer&, const DashboardWidgetLayout&) const {}

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
