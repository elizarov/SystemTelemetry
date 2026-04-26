#include "widget/impl/throughput.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "telemetry/metrics.h"
#include "util/numeric_safety.h"
#include "widget/widget_host.h"

namespace {

using ThroughputGraphLayout = ThroughputWidget::LayoutState;

void FillCircle(WidgetHost& renderer, int centerX, int centerY, int diameter, RenderColorId color) {
    const int radius = diameter / 2;
    renderer.Renderer().FillSolidEllipse(
        RenderRect{centerX - radius, centerY - radius, centerX - radius + diameter, centerY - radius + diameter},
        color);
}

RenderRect MakeAnchorRect(int centerX, int centerY, int representedDiameter, int extraDiameter) {
    const int diameter = std::max(4, representedDiameter + extraDiameter);
    const int radius = diameter / 2;
    return RenderRect{centerX - radius, centerY - radius, centerX - radius + diameter, centerY - radius + diameter};
}

ThroughputGraphLayout ComputeGraphLayout(const WidgetHost& renderer, const RenderRect& rect) {
    ThroughputGraphLayout layout;
    layout.graphRect = rect;
    layout.axisWidth = std::max(1,
        renderer.Renderer().MeasureTextWidth(TextStyleId::Small, "1000") +
            std::max(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.throughput.axisPadding)));
    layout.labelBandHeight = renderer.Renderer().TextMetrics().smallText;
    layout.graphTop = std::min(rect.bottom - 1, rect.top + layout.labelBandHeight);
    layout.graphLeft = rect.left + layout.axisWidth;
    layout.leaderDiameter =
        std::max(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.throughput.leaderDiameter));
    layout.leaderRadius = layout.leaderDiameter / 2;
    layout.plotWidth = std::max<int>(1, rect.right - layout.graphLeft - 1 - layout.leaderRadius);
    layout.graphRight = layout.graphLeft + layout.plotWidth;
    layout.graphBottom = rect.bottom - 1;
    layout.plotStrokeWidth =
        std::max(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.throughput.plotStrokeWidth));
    layout.plotTop = std::min(layout.graphBottom, rect.top + layout.plotStrokeWidth);
    layout.plotHeight = std::max(1, layout.graphBottom - layout.plotTop);
    layout.guideStrokeWidth =
        std::max(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.throughput.guideStrokeWidth));
    layout.guideCenterX = layout.graphLeft + (std::max)(0, (layout.graphRight - layout.graphLeft) / 2);
    layout.guideCenterY = layout.plotTop + (std::max)(0, (layout.graphBottom - layout.plotTop) / 2);
    return layout;
}

void DrawGraph(WidgetHost& renderer,
    const RenderRect& rect,
    const ThroughputGraphLayout& layout,
    const std::vector<double>& history,
    double maxValue,
    double guideStepMbps,
    double timeMarkerOffsetSamples,
    double timeMarkerIntervalSamples,
    const std::optional<LayoutEditAnchorBinding>& maxLabelEditable) {
    renderer.Renderer().FillSolidRect(rect, RenderColorId::GraphBackground);
    maxValue = FiniteNonNegativeOr(maxValue, 10.0);
    if (maxValue <= 0.0) {
        maxValue = 10.0;
    }
    const double guideStep = IsFiniteDouble(guideStepMbps) && guideStepMbps > 0.0 ? guideStepMbps : 5.0;
    const double markerOffset = FiniteNonNegativeOr(timeMarkerOffsetSamples);
    const double markerInterval =
        IsFiniteDouble(timeMarkerIntervalSamples) && timeMarkerIntervalSamples > 0.0 ? timeMarkerIntervalSamples : 20.0;
    const RenderColorId markerColor = RenderColorId::GraphMarker;
    for (double tick = guideStep; tick < maxValue; tick += guideStep) {
        const double ratio = ClampFinite(tick / maxValue, 0.0, 1.0);
        const int centerY = layout.graphBottom - static_cast<int>(std::round(ratio * layout.plotHeight));
        const int lineTop = centerY - (layout.guideStrokeWidth / 2);
        RenderRect lineRect{layout.graphLeft,
            std::max(layout.plotTop, lineTop),
            layout.graphRight,
            std::min(layout.graphBottom + 1, lineTop + layout.guideStrokeWidth)};
        renderer.Renderer().FillSolidRect(lineRect, markerColor);
    }

    if (!history.empty()) {
        for (double sampleOffset = markerOffset;
            sampleOffset <= static_cast<double>(history.size() - 1) + markerInterval;
            sampleOffset += markerInterval) {
            const double clampedOffset = ClampFinite(sampleOffset, 0.0, static_cast<double>(history.size() - 1), 0.0);
            const int centerX =
                layout.graphRight - static_cast<int>(std::round(
                                        clampedOffset * layout.plotWidth / std::max<size_t>(1, history.size() - 1)));
            const int lineLeft = centerX - (layout.guideStrokeWidth / 2);
            RenderRect lineRect{
                lineLeft, rect.top, std::min(layout.graphRight + 1, lineLeft + layout.guideStrokeWidth), rect.bottom};
            renderer.Renderer().FillSolidRect(lineRect, markerColor);
        }
    }

    const RenderColorId axisColor = RenderColorId::GraphAxis;
    const int verticalAxisCenterX = rect.left + layout.axisWidth;
    const int verticalAxisLeft = verticalAxisCenterX - (layout.guideStrokeWidth / 2);
    const int horizontalAxisCenterY = rect.bottom - 1;
    const int horizontalAxisTop = horizontalAxisCenterY - (layout.guideStrokeWidth / 2);
    RenderRect verticalAxisRect{
        verticalAxisLeft, rect.top, std::min(rect.right, verticalAxisLeft + layout.guideStrokeWidth), rect.bottom};
    RenderRect horizontalAxisRect{rect.left + layout.axisWidth,
        horizontalAxisTop,
        rect.right,
        std::min(rect.bottom, horizontalAxisTop + layout.guideStrokeWidth)};
    renderer.Renderer().FillSolidRect(verticalAxisRect, axisColor);
    renderer.Renderer().FillSolidRect(horizontalAxisRect, axisColor);

    char maxLabel[32];
    sprintf_s(maxLabel, "%.0f", maxValue);
    RenderRect maxRect{rect.left, rect.top, rect.left + layout.axisWidth, layout.graphTop};
    if (renderer.CurrentRenderMode() != WidgetHost::RenderMode::Blank) {
        const WidgetHost::TextLayoutResult maxLabelLayout = renderer.Renderer().DrawTextBlock(maxRect,
            maxLabel,
            TextStyleId::Small,
            RenderColorId::MutedText,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
        if (maxLabelEditable.has_value()) {
            renderer.EditArtifacts().RegisterDynamicTextAnchor(
                maxLabelLayout, *maxLabelEditable, WidgetHost::LayoutEditParameter::ColorMutedText);
        }
    }

    if (renderer.CurrentRenderMode() == WidgetHost::RenderMode::Blank) {
        return;
    }

    const RenderColorId plotColor = RenderColorId::Accent;
    const size_t historyDenominator = std::max<size_t>(1, history.size() - 1);
    std::vector<RenderPoint> plotPoints;
    plotPoints.reserve(history.size());
    for (size_t i = 0; i < history.size(); ++i) {
        const double valueRatio = ClampFinite(FiniteNonNegativeOr(history[i]) / maxValue, 0.0, 1.0);
        const int x = layout.graphLeft + static_cast<int>(i * layout.plotWidth / historyDenominator);
        const int y = layout.graphBottom - static_cast<int>(std::round(valueRatio * layout.plotHeight));
        plotPoints.push_back(RenderPoint{x, y});
    }
    if (plotPoints.size() >= 2) {
        renderer.Renderer().DrawPolyline(
            plotPoints, RenderStroke::Solid(plotColor, static_cast<float>(layout.plotStrokeWidth)));
    }

    if (!history.empty() && layout.leaderDiameter > 0) {
        const RenderPoint lastPoint =
            plotPoints.empty() ? RenderPoint{layout.graphLeft, layout.graphBottom} : plotPoints.back();
        FillCircle(renderer, lastPoint.x, lastPoint.y, layout.leaderDiameter, plotColor);
    }
}

int EffectiveThroughputPreferredHeight(const WidgetHost& renderer) {
    const int headerHeight = renderer.Renderer().TextMetrics().smallText;
    const int graphLabelHeight = renderer.Renderer().TextMetrics().smallText;
    return headerHeight + std::max(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.throughput.headerGap)) +
           graphLabelHeight;
}

}  // namespace

WidgetClass ThroughputWidget::Class() const {
    return WidgetClass::Throughput;
}

std::unique_ptr<Widget> ThroughputWidget::Clone() const {
    return std::make_unique<ThroughputWidget>(*this);
}

void ThroughputWidget::Initialize(const LayoutNodeConfig& node) {
    metric_ = node.parameter;
}

int ThroughputWidget::PreferredHeight(const WidgetHost& renderer) const {
    return EffectiveThroughputPreferredHeight(renderer);
}

void ThroughputWidget::ResolveLayoutState(const WidgetHost& renderer, const RenderRect& rect) {
    const int lineHeight = renderer.Renderer().TextMetrics().smallText;
    layoutState_.valueRect =
        RenderRect{rect.left, rect.top, rect.right, (std::min)(rect.bottom, rect.top + lineHeight)};
    layoutState_.graphRect = RenderRect{rect.left,
        (std::min)(rect.bottom,
            layoutState_.valueRect.bottom +
                (std::max)(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.throughput.headerGap))),
        rect.right,
        rect.bottom};
    ThroughputGraphLayout graphLayout = ComputeGraphLayout(renderer, layoutState_.graphRect);
    graphLayout.valueRect = layoutState_.valueRect;
    const int anchorPadding = std::max(1, renderer.Renderer().ScaleLogical(1));
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

void ThroughputWidget::Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const {
    const ThroughputMetric& metric = metrics.ResolveThroughput(metric_);
    renderer.Renderer().DrawText(layoutState_.valueRect,
        metric.label,
        TextStyleId::Small,
        RenderColorId::MutedText,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
    const int labelWidth = renderer.Renderer().MeasureTextWidth(TextStyleId::Small, metric.label);
    renderer.EditArtifacts().RegisterDynamicColorEditRegion(
        WidgetHost::LayoutEditParameter::ColorAccent, layoutState_.graphRect);
    RenderRect numberRect{
        (std::min)(layoutState_.valueRect.right,
            layoutState_.valueRect.left + labelWidth +
                (std::max)(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.throughput.headerGap))),
        layoutState_.valueRect.top,
        layoutState_.valueRect.right,
        layoutState_.valueRect.bottom};
    if (renderer.CurrentRenderMode() != WidgetHost::RenderMode::Blank) {
        const WidgetHost::TextLayoutResult numberLayout = renderer.Renderer().DrawTextBlock(numberRect,
            metric.valueText,
            TextStyleId::Small,
            RenderColorId::Foreground,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Trailing, TextVerticalAlign::Center));
        renderer.EditArtifacts().RegisterDynamicTextAnchor(numberLayout,
            renderer.MakeEditableTextBinding(
                widget, WidgetHost::LayoutEditParameter::FontSmall, 1, renderer.Config().layout.fonts.smallText.size),
            WidgetHost::LayoutEditParameter::ColorForeground);
        renderer.EditArtifacts().RegisterDynamicTextAnchor(
            numberLayout, renderer.MakeMetricTextBinding(widget, metric_, 101));
    }
    const ThroughputGraphLayout& layout = layoutState_;
    DrawGraph(renderer,
        layout.graphRect,
        layout,
        metric.history,
        metric.maxGraph,
        metric.guideStepMbps,
        metric.timeMarkerOffsetSamples,
        metric.timeMarkerIntervalSamples,
        renderer.MakeEditableTextBinding(
            widget, WidgetHost::LayoutEditParameter::FontSmall, 2, renderer.Config().layout.fonts.smallText.size));
}

void ThroughputWidget::BuildStaticAnchors(WidgetHost& renderer, const WidgetLayout& widget) const {
    const ThroughputGraphLayout& layout = layoutState_;
    renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::ThroughputLeaderDiameter,
            0},
        layout.leaderAnchorRect,
        layout.leaderAnchorRect,
        AnchorShape::Circle,
        AnchorDragAxis::Both,
        AnchorDragMode::RadialDistance,
        RenderPoint{layout.leaderAnchorCenterX, layout.leaderAnchorCenterY},
        2.0,
        true,
        true,
        false,
        renderer.Config().layout.throughput.leaderDiameter);

    renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::ThroughputPlotStrokeWidth,
            0},
        layout.plotAnchorRect,
        layout.plotAnchorRect,
        AnchorShape::Circle,
        AnchorDragAxis::Both,
        AnchorDragMode::RadialDistance,
        RenderPoint{layout.plotAnchorCenterX, layout.plotAnchorCenterY},
        2.0,
        true,
        true,
        false,
        renderer.Config().layout.throughput.plotStrokeWidth);

    renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::ThroughputGuideStrokeWidth,
            0},
        layout.guideAnchorRect,
        layout.guideAnchorRect,
        AnchorShape::Circle,
        AnchorDragAxis::Both,
        AnchorDragMode::RadialDistance,
        RenderPoint{layout.guideCenterX, layout.guideCenterY},
        2.0,
        true,
        true,
        false,
        renderer.Config().layout.throughput.guideStrokeWidth);

    const MetricDefinitionConfig* definition = renderer.FindConfiguredMetricDefinition(metric_);
    if (definition != nullptr && !definition->label.empty()) {
        renderer.EditArtifacts().RegisterStaticTextAnchor(layoutState_.valueRect,
            definition->label,
            TextStyleId::Small,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
            renderer.MakeEditableTextBinding(
                widget, WidgetHost::LayoutEditParameter::FontSmall, 0, renderer.Config().layout.fonts.smallText.size),
            WidgetHost::LayoutEditParameter::ColorMutedText);
        renderer.EditArtifacts().RegisterStaticTextAnchor(layoutState_.valueRect,
            definition->label,
            TextStyleId::Small,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
            renderer.MakeMetricTextBinding(widget, metric_, 100));
    }
}

void ThroughputWidget::BuildEditGuides(WidgetHost& renderer, const WidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.Renderer().ScaleLogical(4));

    LayoutEditWidgetGuide guide;
    const int x = std::clamp(static_cast<int>(layoutState_.graphRect.left) + layoutState_.axisWidth,
        static_cast<int>(widget.rect.left),
        static_cast<int>(widget.rect.right));
    guide.axis = LayoutGuideAxis::Vertical;
    guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = WidgetHost::LayoutEditParameter::ThroughputAxisPadding;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = RenderPoint{x, layoutState_.graphRect.top};
    guide.drawEnd = RenderPoint{x, layoutState_.graphRect.bottom};
    guide.hitRect =
        RenderRect{x - hitInset, layoutState_.graphRect.top, x + hitInset + 1, layoutState_.graphRect.bottom};
    guide.value = renderer.Config().layout.throughput.axisPadding;
    guide.dragDirection = 1;
    renderer.EditArtifacts().RegisterWidgetEditGuide(guide);

    const int y = std::clamp(static_cast<int>(layoutState_.graphRect.top),
        static_cast<int>(widget.rect.top),
        static_cast<int>(widget.rect.bottom));
    guide = {};
    guide.axis = LayoutGuideAxis::Horizontal;
    guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = WidgetHost::LayoutEditParameter::ThroughputHeaderGap;
    guide.guideId = 1;
    guide.widgetRect = widget.rect;
    guide.drawStart = RenderPoint{widget.rect.left, y};
    guide.drawEnd = RenderPoint{widget.rect.right, y};
    guide.hitRect = RenderRect{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
    guide.value = renderer.Config().layout.throughput.headerGap;
    guide.dragDirection = 1;
    renderer.EditArtifacts().RegisterWidgetEditGuide(std::move(guide));
}
