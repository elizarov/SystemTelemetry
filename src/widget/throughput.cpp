#include "throughput.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

namespace {

using ThroughputGraphLayout = ThroughputWidget::LayoutState;

COLORREF ToColorRef(unsigned int color) {
    return RGB((color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu);
}

void FillCircle(DashboardRenderer& renderer, HDC hdc, int centerX, int centerY, int diameter, COLORREF color) {
    const int clampedDiameter = std::max(1, diameter);
    const int radius = clampedDiameter / 2;
    HGDIOBJ oldBrush = SelectObject(hdc, renderer.SolidBrush(color));
    HGDIOBJ oldPen = SelectObject(hdc, renderer.SolidPen(color));
    Ellipse(hdc,
        centerX - radius,
        centerY - radius,
        centerX - radius + clampedDiameter,
        centerY - radius + clampedDiameter);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
}

RECT MakeAnchorRect(int centerX, int centerY, int representedDiameter, int extraDiameter) {
    const int diameter = std::max(4, representedDiameter + extraDiameter);
    const int radius = diameter / 2;
    return RECT{centerX - radius, centerY - radius, centerX - radius + diameter, centerY - radius + diameter};
}

ThroughputGraphLayout ComputeGraphLayout(const DashboardRenderer& renderer, const RECT& rect) {
    ThroughputGraphLayout layout;
    layout.graphRect = rect;
    layout.axisWidth = std::max(1,
        renderer.MeasureTextWidth(renderer.WidgetFonts().smallFont, "1000") +
            std::max(0, renderer.ScaleLogical(renderer.Config().layout.throughput.axisPadding)));
    layout.labelBandHeight = renderer.FontMetrics().smallText;
    layout.graphTop = std::min(rect.bottom - 1, rect.top + layout.labelBandHeight);
    layout.graphLeft = rect.left + layout.axisWidth;
    layout.leaderDiameter = std::max(1, renderer.ScaleLogical(renderer.Config().layout.throughput.leaderDiameter));
    layout.leaderRadius = layout.leaderDiameter / 2;
    layout.plotWidth = std::max<int>(1, rect.right - layout.graphLeft - 1 - layout.leaderRadius);
    layout.graphRight = layout.graphLeft + layout.plotWidth;
    layout.graphBottom = rect.bottom - 1;
    layout.plotStrokeWidth = std::max(1, renderer.ScaleLogical(renderer.Config().layout.throughput.plotStrokeWidth));
    layout.plotTop = std::min(layout.graphBottom, static_cast<int>(rect.top) + layout.plotStrokeWidth);
    layout.plotHeight = std::max(1, layout.graphBottom - layout.plotTop);
    layout.guideStrokeWidth = std::max(1, renderer.ScaleLogical(renderer.Config().layout.throughput.guideStrokeWidth));
    layout.guideCenterX = layout.graphLeft + (std::max)(0, (layout.graphRight - layout.graphLeft) / 2);
    layout.guideCenterY = layout.plotTop + (std::max)(0, (layout.graphBottom - layout.plotTop) / 2);
    return layout;
}

void DrawGraph(DashboardRenderer& renderer,
    HDC hdc,
    const RECT& rect,
    const ThroughputGraphLayout& layout,
    const std::vector<double>& history,
    double maxValue,
    double guideStepMbps,
    double timeMarkerOffsetSamples,
    double timeMarkerIntervalSamples,
    const std::optional<DashboardRenderer::EditableAnchorBinding>& maxLabelEditable) {
    HBRUSH bg = renderer.SolidBrush(ToColorRef(renderer.Config().layout.colors.graphBackgroundColor));
    FillRect(hdc, &rect, bg);
    const double guideStep = guideStepMbps > 0.0 ? guideStepMbps : 5.0;
    HBRUSH markerBrush = renderer.SolidBrush(ToColorRef(renderer.Config().layout.colors.graphMarkerColor));
    for (double tick = guideStep; tick < maxValue; tick += guideStep) {
        const double ratio = tick / maxValue;
        const int centerY = layout.graphBottom - static_cast<int>(std::round(ratio * layout.plotHeight));
        const int lineTop = centerY - (layout.guideStrokeWidth / 2);
        RECT lineRect{layout.graphLeft,
            std::max(layout.plotTop, lineTop),
            layout.graphRight,
            std::min(layout.graphBottom + 1, lineTop + layout.guideStrokeWidth)};
        FillRect(hdc, &lineRect, markerBrush);
    }

    if (!history.empty()) {
        const double markerInterval = timeMarkerIntervalSamples > 0.0 ? timeMarkerIntervalSamples : 20.0;
        for (double sampleOffset = timeMarkerOffsetSamples;
            sampleOffset <= static_cast<double>(history.size() - 1) + markerInterval;
            sampleOffset += markerInterval) {
            const double clampedOffset = std::clamp(sampleOffset, 0.0, static_cast<double>(history.size() - 1));
            const int centerX =
                layout.graphRight - static_cast<int>(std::round(
                                        clampedOffset * layout.plotWidth / std::max<size_t>(1, history.size() - 1)));
            const int lineLeft = centerX - (layout.guideStrokeWidth / 2);
            RECT lineRect{
                lineLeft, rect.top, std::min(layout.graphRight + 1, lineLeft + layout.guideStrokeWidth), rect.bottom};
            FillRect(hdc, &lineRect, markerBrush);
        }
    }

    HBRUSH axisBrush = renderer.SolidBrush(ToColorRef(renderer.Config().layout.colors.graphAxisColor));
    const int verticalAxisCenterX = rect.left + layout.axisWidth;
    const int verticalAxisLeft = verticalAxisCenterX - (layout.guideStrokeWidth / 2);
    const int horizontalAxisCenterY = rect.bottom - 1;
    const int horizontalAxisTop = horizontalAxisCenterY - (layout.guideStrokeWidth / 2);
    RECT verticalAxisRect{verticalAxisLeft,
        rect.top,
        std::min<LONG>(rect.right, verticalAxisLeft + layout.guideStrokeWidth),
        rect.bottom};
    RECT horizontalAxisRect{rect.left + layout.axisWidth,
        horizontalAxisTop,
        rect.right,
        std::min<LONG>(rect.bottom, horizontalAxisTop + layout.guideStrokeWidth)};
    FillRect(hdc, &verticalAxisRect, axisBrush);
    FillRect(hdc, &horizontalAxisRect, axisBrush);

    char maxLabel[32];
    sprintf_s(maxLabel, "%.0f", maxValue);
    RECT maxRect{rect.left, rect.top, rect.left + layout.axisWidth, layout.graphTop};
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
        const DashboardRenderer::TextLayoutResult maxLabelLayout = renderer.DrawTextBlock(hdc,
            maxRect,
            maxLabel,
            renderer.WidgetFonts().smallFont,
            renderer.MutedTextColor(),
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        if (maxLabelEditable.has_value()) {
            renderer.RegisterDynamicTextAnchor(maxLabelLayout, *maxLabelEditable);
        }
    }

    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    const COLORREF plotColor = renderer.AccentColor();
    const size_t historyDenominator = std::max<size_t>(1, history.size() - 1);
    std::vector<POINT> plotPoints;
    plotPoints.reserve(history.size());
    for (size_t i = 0; i < history.size(); ++i) {
        const double valueRatio = std::clamp(history[i] / maxValue, 0.0, 1.0);
        const int x = layout.graphLeft + static_cast<int>(i * layout.plotWidth / historyDenominator);
        const int y = layout.graphBottom - static_cast<int>(std::round(valueRatio * layout.plotHeight));
        plotPoints.push_back(POINT{x, y});
    }
    if (plotPoints.size() >= 2) {
        HGDIOBJ oldPen = SelectObject(hdc, renderer.SolidPen(plotColor, layout.plotStrokeWidth));
        Polyline(hdc, plotPoints.data(), static_cast<int>(plotPoints.size()));
        SelectObject(hdc, oldPen);
    }

    if (!history.empty() && layout.leaderDiameter > 0) {
        const POINT lastPoint = plotPoints.empty() ? POINT{layout.graphLeft, layout.graphBottom} : plotPoints.back();
        FillCircle(renderer, hdc, lastPoint.x, lastPoint.y, layout.leaderDiameter, plotColor);
    }
}

int EffectiveThroughputPreferredHeight(const DashboardRenderer& renderer) {
    const int headerHeight = renderer.FontMetrics().smallText;
    const int graphLabelHeight = renderer.FontMetrics().smallText;
    return headerHeight + std::max(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap)) +
           graphLabelHeight;
}

std::string ResolveThroughputLabel(const std::string& metricRef) {
    if (metricRef == "network.upload") {
        return "Up";
    }
    if (metricRef == "network.download") {
        return "Down";
    }
    if (metricRef == "storage.read") {
        return "Read";
    }
    if (metricRef == "storage.write") {
        return "Write";
    }
    return {};
}

}  // namespace

DashboardWidgetClass ThroughputWidget::Class() const {
    return DashboardWidgetClass::Throughput;
}

std::unique_ptr<DashboardWidget> ThroughputWidget::Clone() const {
    return std::make_unique<ThroughputWidget>(*this);
}

void ThroughputWidget::Initialize(const LayoutNodeConfig& node) {
    metric_ = node.parameter;
}

int ThroughputWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return EffectiveThroughputPreferredHeight(renderer);
}

void ThroughputWidget::ResolveLayoutState(const DashboardRenderer& renderer, const RECT& rect) {
    const int lineHeight = renderer.FontMetrics().smallText;
    layoutState_.valueRect =
        RECT{rect.left, rect.top, rect.right, (std::min)(rect.bottom, static_cast<LONG>(rect.top + lineHeight))};
    layoutState_.graphRect = RECT{rect.left,
        (std::min)(rect.bottom,
            static_cast<LONG>(layoutState_.valueRect.bottom +
                              (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap)))),
        rect.right,
        rect.bottom};
    ThroughputGraphLayout graphLayout = ComputeGraphLayout(renderer, layoutState_.graphRect);
    graphLayout.valueRect = layoutState_.valueRect;
    const int anchorPadding = std::max(1, renderer.ScaleLogical(1));
    graphLayout.leaderAnchorCenterX = graphLayout.graphRight;
    graphLayout.leaderAnchorCenterY =
        graphLayout.plotTop + (std::max)(0, (graphLayout.graphBottom - graphLayout.plotTop) / 2);
    graphLayout.leaderAnchorRect = MakeAnchorRect(
        graphLayout.leaderAnchorCenterX, graphLayout.leaderAnchorCenterY, graphLayout.leaderDiameter, anchorPadding);
    graphLayout.plotAnchorCenterX = graphLayout.graphLeft;
    graphLayout.plotAnchorCenterY =
        graphLayout.plotTop + (std::max)(0, (graphLayout.graphBottom - graphLayout.plotTop) / 2);
    graphLayout.plotAnchorRect = MakeAnchorRect(
        graphLayout.plotAnchorCenterX, graphLayout.plotAnchorCenterY, graphLayout.plotStrokeWidth, anchorPadding);
    graphLayout.guideAnchorRect =
        MakeAnchorRect(graphLayout.guideCenterX, graphLayout.guideCenterY, graphLayout.guideStrokeWidth, anchorPadding);
    layoutState_ = graphLayout;
}

void ThroughputWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    const DashboardThroughputMetric& metric = metrics.ResolveThroughput(metric_);
    char buffer[64];
    if (metric.valueMbps >= 100.0) {
        sprintf_s(buffer, "%.0f MB/s", metric.valueMbps);
    } else {
        sprintf_s(buffer, "%.1f MB/s", metric.valueMbps);
    }
    const DashboardRenderer::TextLayoutResult labelLayout = renderer.DrawTextBlock(hdc,
        layoutState_.valueRect,
        metric.label,
        renderer.WidgetFonts().smallFont,
        renderer.MutedTextColor(),
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT numberRect{(std::min)(layoutState_.valueRect.right,
                        labelLayout.textRect.right +
                            (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap))),
        layoutState_.valueRect.top,
        layoutState_.valueRect.right,
        layoutState_.valueRect.bottom};
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
        const DashboardRenderer::TextLayoutResult numberLayout = renderer.DrawTextBlock(hdc,
            numberRect,
            buffer,
            renderer.WidgetFonts().smallFont,
            renderer.ForegroundColor(),
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        renderer.RegisterDynamicTextAnchor(numberLayout,
            renderer.MakeEditableTextBinding(widget,
                DashboardRenderer::LayoutEditParameter::FontSmall,
                1,
                renderer.Config().layout.fonts.smallText.size));
    }
    const ThroughputGraphLayout& layout = layoutState_;
    DrawGraph(renderer,
        hdc,
        layout.graphRect,
        layout,
        metric.history,
        metric.maxGraph,
        metric.guideStepMbps,
        metric.timeMarkerOffsetSamples,
        metric.timeMarkerIntervalSamples,
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::LayoutEditParameter::FontSmall,
            2,
            renderer.Config().layout.fonts.smallText.size));
}

void ThroughputWidget::BuildStaticAnchors(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const ThroughputGraphLayout& layout = layoutState_;
    renderer.RegisterStaticEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::ThroughputLeaderDiameter,
            0,
        },
        layout.leaderAnchorRect,
        layout.leaderAnchorRect,
        DashboardRenderer::AnchorShape::Circle,
        DashboardRenderer::AnchorDragAxis::Both,
        DashboardRenderer::AnchorDragMode::RadialDistance,
        POINT{layout.leaderAnchorCenterX, layout.leaderAnchorCenterY},
        2.0,
        true,
        false,
        renderer.Config().layout.throughput.leaderDiameter);

    renderer.RegisterStaticEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::ThroughputPlotStrokeWidth,
            0,
        },
        layout.plotAnchorRect,
        layout.plotAnchorRect,
        DashboardRenderer::AnchorShape::Circle,
        DashboardRenderer::AnchorDragAxis::Both,
        DashboardRenderer::AnchorDragMode::RadialDistance,
        POINT{layout.plotAnchorCenterX, layout.plotAnchorCenterY},
        2.0,
        true,
        false,
        renderer.Config().layout.throughput.plotStrokeWidth);

    renderer.RegisterStaticEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::ThroughputGuideStrokeWidth,
            0,
        },
        layout.guideAnchorRect,
        layout.guideAnchorRect,
        DashboardRenderer::AnchorShape::Circle,
        DashboardRenderer::AnchorDragAxis::Both,
        DashboardRenderer::AnchorDragMode::RadialDistance,
        POINT{layout.guideCenterX, layout.guideCenterY},
        2.0,
        true,
        false,
        renderer.Config().layout.throughput.guideStrokeWidth);

    const std::string label = ResolveThroughputLabel(metric_);
    if (!label.empty()) {
        renderer.RegisterStaticTextAnchor(layoutState_.valueRect,
            label,
            renderer.WidgetFonts().smallFont,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            renderer.MakeEditableTextBinding(widget,
                DashboardRenderer::LayoutEditParameter::FontSmall,
                0,
                renderer.Config().layout.fonts.smallText.size));
    }
}

void ThroughputWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));

    auto& guides = renderer.WidgetEditGuidesMutable();
    DashboardRenderer::WidgetEditGuide guide;
    const int x = std::clamp(static_cast<int>(layoutState_.graphRect.left) + layoutState_.axisWidth,
        static_cast<int>(widget.rect.left),
        static_cast<int>(widget.rect.right));
    guide.axis = DashboardRenderer::LayoutGuideAxis::Vertical;
    guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::LayoutEditParameter::ThroughputAxisPadding;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = POINT{x, layoutState_.graphRect.top};
    guide.drawEnd = POINT{x, layoutState_.graphRect.bottom};
    guide.hitRect = RECT{x - hitInset, layoutState_.graphRect.top, x + hitInset + 1, layoutState_.graphRect.bottom};
    guide.value = renderer.Config().layout.throughput.axisPadding;
    guide.dragDirection = 1;
    guides.push_back(guide);

    const int y = std::clamp(static_cast<int>(layoutState_.graphRect.top),
        static_cast<int>(widget.rect.top),
        static_cast<int>(widget.rect.bottom));
    guide = {};
    guide.axis = DashboardRenderer::LayoutGuideAxis::Horizontal;
    guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::LayoutEditParameter::ThroughputHeaderGap;
    guide.guideId = 1;
    guide.widgetRect = widget.rect;
    guide.drawStart = POINT{widget.rect.left, y};
    guide.drawEnd = POINT{widget.rect.right, y};
    guide.hitRect = RECT{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
    guide.value = renderer.Config().layout.throughput.headerGap;
    guide.dragDirection = 1;
    guides.push_back(std::move(guide));
}
