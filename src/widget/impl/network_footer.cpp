#include "widget/impl/network_footer.h"

#include "telemetry/metrics.h"
#include "widget/widget_host.h"

WidgetClass NetworkFooterWidget::Class() const {
    return WidgetClass::NetworkFooter;
}

std::unique_ptr<Widget> NetworkFooterWidget::Clone() const {
    return std::make_unique<NetworkFooterWidget>(*this);
}

void NetworkFooterWidget::Initialize(const LayoutNodeConfig&) {}

int NetworkFooterWidget::PreferredHeight(const WidgetHost& renderer) const {
    return renderer.Renderer().TextMetrics().footer +
           (std::max)(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.networkFooter.bottomGap));
}

bool NetworkFooterWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void NetworkFooterWidget::BuildEditGuides(WidgetHost& renderer, const WidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.Renderer().ScaleLogical(4));
    const int y = widget.rect.top + renderer.Renderer().TextMetrics().footer;

    LayoutEditWidgetGuide guide;
    guide.axis = LayoutGuideAxis::Horizontal;
    guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = WidgetHost::LayoutEditParameter::NetworkFooterBottomGap;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = RenderPoint{widget.rect.left, y};
    guide.drawEnd = RenderPoint{widget.rect.right, y};
    guide.hitRect = RenderRect{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
    guide.value = renderer.Config().layout.networkFooter.bottomGap;
    guide.dragDirection = -1;
    renderer.WidgetEditGuidesMutable().push_back(std::move(guide));
}

void NetworkFooterWidget::Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == WidgetHost::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveNetworkFooter();
    const WidgetHost::TextLayoutResult textLayout = renderer.Renderer().DrawTextBlock(widget.rect,
        text,
        TextStyleId::Footer,
        RenderColorId::MutedText,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, true));
    renderer.RegisterDynamicTextAnchor(textLayout,
        renderer.MakeEditableTextBinding(
            widget, WidgetHost::LayoutEditParameter::FontFooter, 0, renderer.Config().layout.fonts.footer.size),
        WidgetHost::LayoutEditParameter::ColorMutedText);
}
