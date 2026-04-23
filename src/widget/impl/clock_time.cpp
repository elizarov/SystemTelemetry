#include "widget/impl/clock_time.h"

#include "telemetry/metrics.h"
#include "widget/widget_renderer.h"

DashboardWidgetClass ClockTimeWidget::Class() const {
    return DashboardWidgetClass::ClockTime;
}

std::unique_ptr<DashboardWidget> ClockTimeWidget::Clone() const {
    return std::make_unique<ClockTimeWidget>(*this);
}

void ClockTimeWidget::Initialize(const LayoutNodeConfig&) {}

int ClockTimeWidget::PreferredHeight(const WidgetRenderer& renderer) const {
    return renderer.TextMetrics().clockTime;
}

bool ClockTimeWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void ClockTimeWidget::Draw(
    WidgetRenderer& renderer, const DashboardWidgetLayout& widget, const MetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == WidgetRenderer::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveClockTime();
    const WidgetRenderer::TextLayoutResult textLayout = renderer.DrawTextBlock(widget.rect,
        text,
        TextStyleId::ClockTime,
        RenderColorId::Foreground,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
    renderer.RegisterDynamicTextAnchor(textLayout,
        renderer.MakeEditableTextBinding(widget,
            WidgetRenderer::LayoutEditParameter::FontClockTime,
            0,
            renderer.Config().layout.fonts.clockTime.size),
        WidgetRenderer::LayoutEditParameter::ColorForeground);
}
