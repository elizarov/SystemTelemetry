#include "network_footer.h"

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

DashboardWidgetClass NetworkFooterWidget::Class() const {
    return DashboardWidgetClass::NetworkFooter;
}

std::unique_ptr<DashboardWidget> NetworkFooterWidget::Clone() const {
    return std::make_unique<NetworkFooterWidget>(*this);
}

void NetworkFooterWidget::Initialize(const LayoutNodeConfig&) {}

int NetworkFooterWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.TextMetrics().footer +
           (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.networkFooter.bottomGap));
}

bool NetworkFooterWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void NetworkFooterWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    const int y = widget.rect.top + renderer.TextMetrics().footer;

    LayoutEditWidgetGuide guide;
    guide.axis = LayoutGuideAxis::Horizontal;
    guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::LayoutEditParameter::NetworkFooterBottomGap;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = RenderPoint{widget.rect.left, y};
    guide.drawEnd = RenderPoint{widget.rect.right, y};
    guide.hitRect = RenderRect{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
    guide.value = renderer.Config().layout.networkFooter.bottomGap;
    guide.dragDirection = -1;
    renderer.WidgetEditGuidesMutable().push_back(std::move(guide));
}

void NetworkFooterWidget::Draw(
    DashboardRenderer& renderer, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveNetworkFooter();
    const DashboardRenderer::TextLayoutResult textLayout = renderer.DrawTextBlock(widget.rect,
        text,
        TextStyleId::Footer,
        renderer.MutedTextColor(),
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, true));
    renderer.RegisterDynamicTextAnchor(textLayout,
        renderer.MakeEditableTextBinding(
            widget, DashboardRenderer::LayoutEditParameter::FontFooter, 0, renderer.Config().layout.fonts.footer.size),
        DashboardRenderer::LayoutEditParameter::ColorMutedText);
}
