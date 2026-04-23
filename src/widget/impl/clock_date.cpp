#include "widget/impl/clock_date.h"

#include "telemetry/metrics.h"
#include "widget/widget_renderer.h"

WidgetClass ClockDateWidget::Class() const {
    return WidgetClass::ClockDate;
}

std::unique_ptr<Widget> ClockDateWidget::Clone() const {
    return std::make_unique<ClockDateWidget>(*this);
}

void ClockDateWidget::Initialize(const LayoutNodeConfig&) {}

int ClockDateWidget::PreferredHeight(const WidgetRenderer& renderer) const {
    return renderer.TextMetrics().clockDate;
}

bool ClockDateWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void ClockDateWidget::Draw(WidgetRenderer& renderer, const WidgetLayout& widget, const MetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == WidgetRenderer::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveClockDate();
    const WidgetRenderer::TextLayoutResult textLayout = renderer.DrawTextBlock(widget.rect,
        text,
        TextStyleId::ClockDate,
        RenderColorId::MutedText,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
    renderer.RegisterDynamicTextAnchor(textLayout,
        renderer.MakeEditableTextBinding(widget,
            WidgetRenderer::LayoutEditParameter::FontClockDate,
            0,
            renderer.Config().layout.fonts.clockDate.size),
        WidgetRenderer::LayoutEditParameter::ColorMutedText);
}
