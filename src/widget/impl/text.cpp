#include "widget/impl/text.h"

#include <algorithm>

#include "telemetry/metrics.h"
#include "widget/widget_renderer.h"

DashboardWidgetClass TextWidget::Class() const {
    return DashboardWidgetClass::Text;
}

std::unique_ptr<DashboardWidget> TextWidget::Clone() const {
    return std::make_unique<TextWidget>(*this);
}

void TextWidget::Initialize(const LayoutNodeConfig& node) {
    metric_ = node.parameter;
    staticAnchorRegistered_ = false;
    cachedStaticText_.clear();
}

int TextWidget::PreferredHeight(const WidgetRenderer& renderer) const {
    return renderer.TextMetrics().text + (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.text.bottomGap));
}

bool TextWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void TextWidget::BuildEditGuides(WidgetRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    const int y = widget.rect.bottom;

    LayoutEditWidgetGuide guide;
    guide.axis = LayoutGuideAxis::Horizontal;
    guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = WidgetRenderer::LayoutEditParameter::TextBottomGap;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = RenderPoint{widget.rect.left, y};
    guide.drawEnd = RenderPoint{widget.rect.right, y};
    guide.hitRect = RenderRect{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
    guide.value = renderer.Config().layout.text.bottomGap;
    guide.dragDirection = 1;
    renderer.WidgetEditGuidesMutable().push_back(std::move(guide));
}

void TextWidget::Draw(
    WidgetRenderer& renderer, const DashboardWidgetLayout& widget, const MetricSource& metrics) const {
    const std::string text = metrics.ResolveText(metric_);
    const WidgetRenderer::TextLayoutResult textLayout = renderer.DrawTextBlock(widget.rect,
        text,
        TextStyleId::Text,
        RenderColorId::Foreground,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, true));
    const auto binding = renderer.MakeEditableTextBinding(
        widget, WidgetRenderer::LayoutEditParameter::FontText, 0, renderer.Config().layout.fonts.text.size);
    if (IsStaticTextMetric(metric_)) {
        if (!staticAnchorRegistered_) {
            cachedStaticText_ = text;
            renderer.RegisterStaticTextAnchor(widget.rect,
                cachedStaticText_,
                TextStyleId::Text,
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, true),
                binding,
                WidgetRenderer::LayoutEditParameter::ColorForeground);
            staticAnchorRegistered_ = true;
        }
        return;
    }
    renderer.RegisterDynamicTextAnchor(textLayout, binding, WidgetRenderer::LayoutEditParameter::ColorForeground);
}
