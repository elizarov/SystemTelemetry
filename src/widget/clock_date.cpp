#include "widget/clock_date.h"

#include "dashboard_metrics.h"
#include "dashboard_renderer.h"

DashboardWidgetClass ClockDateWidget::Class() const {
    return DashboardWidgetClass::ClockDate;
}

std::unique_ptr<DashboardWidget> ClockDateWidget::Clone() const {
    return std::make_unique<ClockDateWidget>(*this);
}

void ClockDateWidget::Initialize(const LayoutNodeConfig&) {}

int ClockDateWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.TextMetrics().clockDate;
}

bool ClockDateWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void ClockDateWidget::Draw(
    DashboardRenderer& renderer, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveClockDate();
    const DashboardRenderer::TextLayoutResult textLayout = renderer.DrawTextBlock(widget.rect,
        text,
        TextStyleId::ClockDate,
        renderer.ColorPalette().mutedText,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
    renderer.RegisterDynamicTextAnchor(textLayout,
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::LayoutEditParameter::FontClockDate,
            0,
            renderer.Config().layout.fonts.clockDate.size),
        DashboardRenderer::LayoutEditParameter::ColorMutedText);
}
