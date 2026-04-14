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
    return renderer.FontMetrics().footer +
           (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.networkFooter.bottomGap));
}

bool NetworkFooterWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void NetworkFooterWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    const int y = widget.rect.top + renderer.FontMetrics().footer;

    DashboardRenderer::WidgetEditGuide guide;
    guide.axis = DashboardRenderer::LayoutGuideAxis::Horizontal;
    guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::LayoutEditParameter::NetworkFooterBottomGap;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = POINT{widget.rect.left, y};
    guide.drawEnd = POINT{widget.rect.right, y};
    guide.hitRect = RECT{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
    guide.value = renderer.Config().layout.networkFooter.bottomGap;
    guide.dragDirection = -1;
    renderer.WidgetEditGuidesMutable().push_back(std::move(guide));
}

void NetworkFooterWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveNetworkFooter();
    const DashboardRenderer::TextLayoutResult textLayout = renderer.DrawTextBlock(hdc,
        widget.rect,
        text,
        renderer.WidgetFonts().footer,
        renderer.MutedTextColor(),
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    renderer.RegisterDynamicTextAnchor(textLayout,
        renderer.MakeEditableTextBinding(
            widget, DashboardRenderer::LayoutEditParameter::FontFooter, 0, renderer.Config().layout.fonts.footer.size));
}
