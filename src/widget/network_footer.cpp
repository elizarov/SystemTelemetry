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
           (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.networkFooter.preferredPadding));
}

bool NetworkFooterWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void NetworkFooterWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    const std::string text = metrics.ResolveNetworkFooter();
    renderer.DrawTextBlock(hdc,
        widget.rect,
        text,
        renderer.WidgetFonts().footer,
        renderer.MutedTextColor(),
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    renderer.RegisterDynamicTextAnchor(widget.rect,
        text,
        renderer.WidgetFonts().footer,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS,
        renderer.MakeEditableTextBinding(
            widget, DashboardRenderer::LayoutEditParameter::FontFooter, 0, renderer.Config().layout.fonts.footer.size));
}
