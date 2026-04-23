#include "widget/widget.h"

#include "widget/impl/card_chrome.h"
#include "widget/impl/clock_date.h"
#include "widget/impl/clock_time.h"
#include "widget/impl/drive_usage_list.h"
#include "widget/impl/gauge.h"
#include "widget/impl/metric_list.h"
#include "widget/impl/network_footer.h"
#include "widget/impl/text.h"
#include "widget/impl/throughput.h"
#include "widget/impl/vertical_spacer.h"
#include "widget/impl/vertical_spring.h"

bool Widget::UsesFixedPreferredHeightInRows() const {
    return false;
}

bool Widget::IsHoverable() const {
    return true;
}

bool Widget::IsVerticalSpring() const {
    return false;
}

void Widget::ResolveLayoutState(const WidgetRenderer&, const RenderRect&) {}

void Widget::Draw(WidgetRenderer&, const WidgetLayout&, const MetricSource&) const {}

void Widget::FinalizeLayoutGroup(WidgetRenderer&, const std::vector<WidgetLayout*>&) {}

void Widget::BuildEditGuides(WidgetRenderer&, const WidgetLayout&) const {}

void Widget::BuildStaticAnchors(WidgetRenderer&, const WidgetLayout&) const {}

std::unique_ptr<Widget> CreateWidget(WidgetClass widgetClass) {
    switch (widgetClass) {
        case WidgetClass::Text:
            return std::make_unique<TextWidget>();
        case WidgetClass::Gauge:
            return std::make_unique<GaugeWidget>();
        case WidgetClass::MetricList:
            return std::make_unique<MetricListWidget>();
        case WidgetClass::Throughput:
            return std::make_unique<ThroughputWidget>();
        case WidgetClass::NetworkFooter:
            return std::make_unique<NetworkFooterWidget>();
        case WidgetClass::VerticalSpacer:
            return std::make_unique<VerticalSpacerWidget>();
        case WidgetClass::VerticalSpring:
            return std::make_unique<VerticalSpringWidget>();
        case WidgetClass::DriveUsageList:
            return std::make_unique<DriveUsageListWidget>();
        case WidgetClass::ClockTime:
            return std::make_unique<ClockTimeWidget>();
        case WidgetClass::ClockDate:
            return std::make_unique<ClockDateWidget>();
        case WidgetClass::Unknown:
        default:
            return nullptr;
    }
}

std::unique_ptr<Widget> CreateWidget(std::string_view name) {
    const auto widgetClass = name.empty() ? std::nullopt : EnumFromString<WidgetClass>(name);
    if (!widgetClass.has_value()) {
        return nullptr;
    }
    return CreateWidget(*widgetClass);
}

std::unique_ptr<Widget> CreateCardChromeWidget(const LayoutCardConfig& card) {
    return std::make_unique<CardChromeWidget>(card);
}
