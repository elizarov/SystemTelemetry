#include "widget/impl/clock_time.h"

#include "dashboard/dashboard_metrics.h"
#include "dashboard_renderer/dashboard_renderer.h"

DashboardWidgetClass ClockTimeWidget::Class() const {
    return DashboardWidgetClass::ClockTime;
}

std::unique_ptr<DashboardWidget> ClockTimeWidget::Clone() const {
    return std::make_unique<ClockTimeWidget>(*this);
}

void ClockTimeWidget::Initialize(const LayoutNodeConfig&) {}

int ClockTimeWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.TextMetrics().clockTime;
}

bool ClockTimeWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void ClockTimeWidget::Draw(
    DashboardRenderer& renderer, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveClockTime();
    const DashboardRenderer::TextLayoutResult textLayout = renderer.DrawTextBlock(widget.rect,
        text,
        TextStyleId::ClockTime,
        RenderColorId::Foreground,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
    renderer.RegisterDynamicTextAnchor(textLayout,
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::LayoutEditParameter::FontClockTime,
            0,
            renderer.Config().layout.fonts.clockTime.size),
        DashboardRenderer::LayoutEditParameter::ColorForeground);
}
