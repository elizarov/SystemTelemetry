#include "widget/widget.h"

#include "widget/impl/clock_date.h"
#include "widget/impl/clock_time.h"
#include "widget/impl/drive_usage_list.h"
#include "widget/impl/gauge.h"
#include "widget/impl/metric_list.h"
#include "widget/impl/network_footer.h"
#include "widget/impl/vertical_spacer.h"
#include "widget/impl/text.h"
#include "widget/impl/throughput.h"
#include "widget/impl/vertical_spring.h"

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
    const auto widgetClass = name.empty() ? std::nullopt : EnumFromString<DashboardWidgetClass>(name);
    if (!widgetClass.has_value()) {
        return nullptr;
    }
    return CreateDashboardWidget(*widgetClass);
}
