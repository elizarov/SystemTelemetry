#include "widget/impl/clock_time.h"

#include <algorithm>

#include "telemetry/metrics.h"
#include "widget/widget_host.h"

WidgetClass ClockTimeWidget::Class() const {
    return WidgetClass::ClockTime;
}

std::unique_ptr<Widget> ClockTimeWidget::Clone() const {
    return std::make_unique<ClockTimeWidget>(*this);
}

void ClockTimeWidget::Initialize(const LayoutNodeConfig& node) {
    format_ = node.parameter;
}

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

    const std::string text = !format_.empty() ? metrics.ResolveClockTime(format_) : std::string();
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

void ClockTimeWidget::BuildStaticAnchors(WidgetHost& renderer, const WidgetLayout& widget) const {
    const int anchorSize = std::max(6, renderer.Renderer().ScaleLogical(8));
    const RenderRect anchorRect{
        widget.rect.left,
        widget.rect.top,
        widget.rect.left + anchorSize,
        widget.rect.top + anchorSize,
    };
    renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            LayoutDateTimeFormatEditKey{widget.editCardId, widget.nodePath, WidgetClass::ClockTime},
            0},
        widget.rect,
        anchorRect,
        AnchorShape::Wedge,
        AnchorDragAxis::Vertical,
        AnchorDragMode::AxisDelta,
        anchorRect.Center(),
        1.0,
        false,
        true,
        false,
        0);
}
