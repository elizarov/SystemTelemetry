#include "text.h"

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

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

int TextWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.FontMetrics().text +
           (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.text.preferredPadding));
}

bool TextWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

void TextWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    const std::string text = metrics.ResolveText(metric_);
    renderer.DrawTextBlock(hdc,
        widget.rect,
        text,
        renderer.WidgetFonts().text,
        renderer.ForegroundColor(),
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    const auto binding = renderer.MakeEditableTextBinding(
        widget, DashboardRenderer::AnchorEditParameter::FontText, 0, renderer.Config().layout.fonts.text.size);
    if (metric_ == "cpu.name" || metric_ == "gpu.name") {
        if (!staticAnchorRegistered_) {
            cachedStaticText_ = text;
            renderer.RegisterStaticTextAnchor(
                widget.rect, cachedStaticText_, renderer.WidgetFonts().text, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, binding);
            staticAnchorRegistered_ = true;
        }
        return;
    }
    renderer.RegisterDynamicTextAnchor(
        widget.rect, text, renderer.WidgetFonts().text, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, binding);
}
