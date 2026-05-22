#include "widget/impl/clock_date.h"

#include "telemetry/metrics.h"
#include "widget/widget_host.h"

void ClockDateWidget::Initialize(const LayoutNodeConfig& node) {
    format_ = node.parameter;
}

int ClockDateWidget::PreferredHeight(const WidgetHost& renderer) const {
    return renderer.Renderer().TextMetrics().clockDate;
}

void ClockDateWidget::Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == WidgetHost::RenderMode::Blank) {
        return;
    }

    const std::string text = !format_.empty() ? metrics.ResolveClockDate(format_) : std::string();
    const WidgetHost::TextLayoutResult textLayout = renderer.Renderer().DrawTextBlock(widget.rect,
        text,
        TextStyleId::ClockDate,
        RenderColorId::MutedText,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
    renderer.EditArtifacts().RegisterDynamicTextAnchor(textLayout,
        renderer.MakeEditableTextBinding(
            widget, WidgetHost::LayoutEditParameter::FontClockDate, 0, renderer.Config().layout.fonts.clockDate.size),
        WidgetHost::LayoutEditParameter::ColorMutedText);
    renderer.EditArtifacts().RegisterDynamicCornerEditAnchor(
        MakeLayoutNodeFieldEditAnchorKey(widget, WidgetClass::ClockDate), textLayout.textRect);
}

void ClockDateWidget::BuildStaticAnchors(WidgetHost&, const WidgetLayout&) const {}
