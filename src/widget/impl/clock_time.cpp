#include "widget/impl/clock_time.h"

#include "telemetry/metrics.h"
#include "widget/widget_host.h"

WidgetClass ClockTimeWidget::Class() const {
    return WidgetClass::ClockTime;
}

std::unique_ptr<Widget> ClockTimeWidget::Clone() const {
    return std::make_unique<ClockTimeWidget>(*this);
}

void ClockTimeWidget::Initialize(const LayoutNodeConfig&) {}

int ClockTimeWidget::PreferredHeight(const WidgetHost& renderer) const {
    return renderer.Renderer().TextMetrics().clockTime;
}

bool ClockTimeWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void ClockTimeWidget::Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == WidgetHost::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveClockTime();
    const WidgetHost::TextLayoutResult textLayout = renderer.Renderer().DrawTextBlock(widget.rect,
        text,
        TextStyleId::ClockTime,
        RenderColorId::Foreground,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
    renderer.EditArtifacts().RegisterDynamicTextAnchor(textLayout,
        renderer.MakeEditableTextBinding(
            widget, WidgetHost::LayoutEditParameter::FontClockTime, 0, renderer.Config().layout.fonts.clockTime.size),
        WidgetHost::LayoutEditParameter::ColorForeground);
}
