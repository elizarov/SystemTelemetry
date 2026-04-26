#include "widget/impl/text.h"

#include <algorithm>

#include "telemetry/metrics.h"
#include "widget/widget_host.h"

WidgetClass TextWidget::Class() const {
    return WidgetClass::Text;
}

std::unique_ptr<Widget> TextWidget::Clone() const {
    return std::make_unique<TextWidget>(*this);
}

void TextWidget::Initialize(const LayoutNodeConfig& node) {
    metric_ = node.parameter;
}

int TextWidget::PreferredHeight(const WidgetHost& renderer) const {
    return renderer.Renderer().TextMetrics().text +
           (std::max)(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.text.bottomGap));
}

bool TextWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void TextWidget::BuildEditGuides(WidgetHost& renderer, const WidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.Renderer().ScaleLogical(4));
    const int y = widget.rect.bottom;

    LayoutEditWidgetGuide guide;
    guide.axis = LayoutGuideAxis::Horizontal;
    guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = WidgetHost::LayoutEditParameter::TextBottomGap;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = RenderPoint{widget.rect.left, y};
    guide.drawEnd = RenderPoint{widget.rect.right, y};
    guide.hitRect = RenderRect{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
    guide.value = renderer.Config().layout.text.bottomGap;
    guide.dragDirection = 1;
    renderer.EditArtifacts().RegisterWidgetEditGuide(std::move(guide));
}

void TextWidget::Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const {
    const std::string text = metrics.ResolveText(metric_);
    const WidgetHost::TextLayoutResult textLayout = renderer.Renderer().DrawTextBlock(widget.rect,
        text,
        TextStyleId::Text,
        RenderColorId::Foreground,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, true));
    const auto binding = renderer.MakeEditableTextBinding(
        widget, WidgetHost::LayoutEditParameter::FontText, 0, renderer.Config().layout.fonts.text.size);
    renderer.EditArtifacts().RegisterDynamicTextAnchor(
        textLayout, binding, WidgetHost::LayoutEditParameter::ColorForeground);
}
