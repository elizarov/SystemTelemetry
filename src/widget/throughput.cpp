#include "throughput.h"

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

const char* ThroughputWidget::TypeName() const {
    return "throughput";
}

void ThroughputWidget::Initialize(const LayoutNodeConfig& node) {
    metric_ = node.parameter;
}

int ThroughputWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.EffectiveThroughputPreferredHeight();
}

void ThroughputWidget::Draw(
    DashboardRenderer& renderer, HDC hdc, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    const DashboardThroughputMetric metric = metrics.ResolveThroughput(metric_);
    const int lineHeight = renderer.FontMetrics().smallText;
    RECT valueRect{widget.rect.left, widget.rect.top, widget.rect.right, (std::min)(widget.rect.bottom, widget.rect.top + lineHeight)};
    RECT graphRect{widget.rect.left,
        (std::min)(widget.rect.bottom,
            valueRect.bottom + (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap))),
        widget.rect.right,
        widget.rect.bottom};
    char buffer[64];
    if (metric.valueMbps >= 100.0) {
        sprintf_s(buffer, "%.0f MB/s", metric.valueMbps);
    } else {
        sprintf_s(buffer, "%.1f MB/s", metric.valueMbps);
    }
    const DashboardRenderer::TextLayoutResult labelLayout = renderer.DrawTextBlock(hdc,
        valueRect,
        metric.label,
        renderer.WidgetFonts().smallFont,
        renderer.MutedTextColor(),
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontSmall, 0,
            renderer.Config().layout.fonts.smallText.size));
    RECT numberRect{
        (std::min)(valueRect.right,
            labelLayout.textRect.right + (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap))),
        valueRect.top,
        valueRect.right,
        valueRect.bottom};
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
        renderer.DrawTextBlock(hdc,
            numberRect,
            buffer,
            renderer.WidgetFonts().smallFont,
            renderer.ForegroundColor(),
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
            renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontSmall, 1,
                renderer.Config().layout.fonts.smallText.size));
    }
    renderer.DrawGraph(hdc,
        graphRect,
        metric.history,
        metric.maxGraph,
        metric.guideStepMbps,
        metric.timeMarkerOffsetSamples,
        metric.timeMarkerIntervalSamples,
        renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontSmall, 2,
            renderer.Config().layout.fonts.smallText.size));
}

void ThroughputWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int lineHeight = renderer.FontMetrics().smallText;
    RECT valueRect{widget.rect.left, widget.rect.top, widget.rect.right, (std::min)(widget.rect.bottom, widget.rect.top + lineHeight)};
    RECT graphRect{widget.rect.left,
        (std::min)(widget.rect.bottom,
            valueRect.bottom + (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap))),
        widget.rect.right,
        widget.rect.bottom};
    const int axisWidth = (std::max)(1, renderer.MeasuredTextWidths().throughputAxis);
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));

    auto& guides = renderer.WidgetEditGuidesMutable();
    DashboardRenderer::WidgetEditGuide guide;
    const int x = std::clamp(static_cast<int>(graphRect.left) + axisWidth,
        static_cast<int>(widget.rect.left),
        static_cast<int>(widget.rect.right));
    guide.axis = DashboardRenderer::LayoutGuideAxis::Vertical;
    guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::WidgetEditParameter::ThroughputAxisPadding;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = POINT{x, graphRect.top};
    guide.drawEnd = POINT{x, graphRect.bottom};
    guide.hitRect = RECT{x - hitInset, graphRect.top, x + hitInset + 1, graphRect.bottom};
    guide.value = renderer.Config().layout.throughput.axisPadding;
    guide.dragDirection = 1;
    guides.push_back(guide);

    const int y = std::clamp(static_cast<int>(graphRect.top), static_cast<int>(widget.rect.top), static_cast<int>(widget.rect.bottom));
    guide = {};
    guide.axis = DashboardRenderer::LayoutGuideAxis::Horizontal;
    guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::WidgetEditParameter::ThroughputHeaderGap;
    guide.guideId = 1;
    guide.widgetRect = widget.rect;
    guide.drawStart = POINT{widget.rect.left, y};
    guide.drawEnd = POINT{widget.rect.right, y};
    guide.hitRect = RECT{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
    guide.value = renderer.Config().layout.throughput.headerGap;
    guide.dragDirection = 1;
    guides.push_back(std::move(guide));
}
