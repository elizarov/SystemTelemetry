#include "widget/impl/throughput.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

#include "telemetry/metrics.h"
#include "util/numeric_safety.h"
#include "widget/impl/animation_primitives.h"
#include "widget/widget_host.h"

namespace {

using ThroughputGraphLayout = ThroughputWidget::LayoutState;

struct PlotPoint {
    double x = 0.0;
    double y = 0.0;
};

constexpr double kPlotEpsilon = 0.000001;

void FillCircle(Renderer& renderer, int centerX, int centerY, int diameter, RenderColorId color) {
    const int radius = diameter / 2;
    renderer.FillSolidEllipse(
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

size_t PlotTailCount(double plotShiftSamples, size_t sampleCount) {
    if (plotShiftSamples <= 0.0 || sampleCount == 0) {
        return 0;
    }
    const size_t tailCount = static_cast<size_t>(std::ceil(plotShiftSamples - kPlotEpsilon));
    return (std::min)(tailCount, sampleCount - 1);
}

size_t VisibleSampleCount(size_t sampleCount, double plotShiftSamples) {
    const size_t tailCount = PlotTailCount(plotShiftSamples, sampleCount);
    return (std::max<size_t>)(1, sampleCount - tailCount);
}

double NormalizeMarkerOffsetSamples(double timeMarkerOffsetSamples, double timeMarkerIntervalSamples) {
    const double normalized = std::fmod(FiniteNonNegativeOr(timeMarkerOffsetSamples), timeMarkerIntervalSamples);
    if (normalized + kPlotEpsilon >= timeMarkerIntervalSamples) {
        return 0.0;
    }
    return normalized;
}

void AddRenderPoint(std::vector<RenderPoint>& points, double x, double y) {
    const RenderPoint point{static_cast<int>(std::round(x)), static_cast<int>(std::round(y))};
    if (!points.empty() && points.back().x == point.x && points.back().y == point.y) {
        return;
    }
    points.push_back(point);
}

double InterpolatePlotYAtX(const PlotPoint& start, const PlotPoint& end, double x) {
    if (std::abs(end.x - start.x) <= kPlotEpsilon) {
        return end.y;
    }
    const double progress = std::clamp((x - start.x) / (end.x - start.x), 0.0, 1.0);
    return start.y + ((end.y - start.y) * progress);
}

std::vector<RenderPoint> ClipPlotPointsToGraph(const std::vector<PlotPoint>& points, int left, int right) {
    std::vector<RenderPoint> clipped;
    if (points.empty() || right < left) {
        return clipped;
    }

    if (points.front().x >= left && points.front().x <= right) {
        AddRenderPoint(clipped, points.front().x, points.front().y);
    }

    for (size_t index = 1; index < points.size(); ++index) {
        const PlotPoint& start = points[index - 1];
        const PlotPoint& end = points[index];
        if (start.x < left && end.x >= left) {
            AddRenderPoint(clipped, static_cast<double>(left), InterpolatePlotYAtX(start, end, left));
        }
        if (end.x >= left && end.x <= right) {
            AddRenderPoint(clipped, end.x, end.y);
        }
        if (start.x <= right && end.x > right) {
            AddRenderPoint(clipped, static_cast<double>(right), InterpolatePlotYAtX(start, end, right));
            break;
        }
    }
    return clipped;
}

std::optional<double> PlotYAtX(const std::vector<PlotPoint>& points, int x) {
    if (points.empty()) {
        return std::nullopt;
    }
    if (points.size() == 1) {
        return points.front().y;
    }
    for (size_t index = 1; index < points.size(); ++index) {
        const PlotPoint& start = points[index - 1];
        const PlotPoint& end = points[index];
        if (start.x <= x && end.x >= x) {
            return InterpolatePlotYAtX(start, end, static_cast<double>(x));
        }
    }
    if (points.back().x < x) {
        return points.back().y;
    }
    return std::nullopt;
}

void DrawGraphSnapshot(WidgetHost& renderer,
    const RenderRect& rect,
    const ThroughputGraphLayout& layout,
    double maxValue,
    const std::optional<LayoutEditAnchorBinding>& maxLabelEditable) {
    renderer.Renderer().FillSolidRect(rect, RenderColorId::GraphBackground);
    maxValue = FiniteNonNegativeOr(maxValue, 10.0);
    if (maxValue <= 0.0) {
        maxValue = 10.0;
    }
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
}

void DrawGraphAnimated(Renderer& renderer,
    const RenderRect& rect,
    const ThroughputGraphLayout& layout,
    const std::vector<double>& history,
    double maxValue,
    double guideStepMbps,
    double timeMarkerOffsetSamples,
    double plotShiftSamples,
    double timeMarkerIntervalSamples,
    double labelMaxValue,
    bool drawValues) {
    maxValue = FiniteNonNegativeOr(maxValue, 10.0);
    if (maxValue <= 0.0) {
        maxValue = 10.0;
    }
    const double guideStep = IsFiniteDouble(guideStepMbps) && guideStepMbps > 0.0 ? guideStepMbps : 5.0;
    const double markerInterval =
        IsFiniteDouble(timeMarkerIntervalSamples) && timeMarkerIntervalSamples > 0.0 ? timeMarkerIntervalSamples : 20.0;
    const double plotShift = FiniteNonNegativeOr(plotShiftSamples);
    const size_t visibleSampleCount = VisibleSampleCount(history.size(), plotShift);
    const size_t historyDenominator = std::max<size_t>(1, visibleSampleCount - 1);
    const double markerOffset = NormalizeMarkerOffsetSamples(timeMarkerOffsetSamples, markerInterval);
    const RenderColorId markerColor = RenderColorId::GraphMarker;
    for (double tick = guideStep; tick < maxValue; tick += guideStep) {
        const double ratio = ClampFinite(tick / maxValue, 0.0, 1.0);
        const int centerY = layout.graphBottom - static_cast<int>(std::round(ratio * layout.plotHeight));
        const int lineTop = centerY - (layout.guideStrokeWidth / 2);
        RenderRect lineRect{layout.graphLeft,
            std::max(layout.plotTop, lineTop),
            layout.graphRight,
            std::min(layout.graphBottom + 1, lineTop + layout.guideStrokeWidth)};
        renderer.FillSolidRect(lineRect, markerColor);
    }

    if (!history.empty()) {
        const double visibleDenominator = static_cast<double>(historyDenominator);
        for (double sampleOffset = markerOffset; sampleOffset <= visibleDenominator + kPlotEpsilon;
            sampleOffset += markerInterval) {
            const int centerX =
                layout.graphRight - static_cast<int>(std::round(sampleOffset * layout.plotWidth / visibleDenominator));
            const int lineLeft = centerX - (layout.guideStrokeWidth / 2);
            RenderRect lineRect{
                lineLeft, rect.top, std::min(layout.graphRight + 1, lineLeft + layout.guideStrokeWidth), rect.bottom};
            renderer.FillSolidRect(lineRect, markerColor);
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
    renderer.FillSolidRect(verticalAxisRect, axisColor);
    renderer.FillSolidRect(horizontalAxisRect, axisColor);

    if (!drawValues) {
        return;
    }

    labelMaxValue = FiniteNonNegativeOr(labelMaxValue, 10.0);
    if (labelMaxValue <= 0.0) {
        labelMaxValue = 10.0;
    }
    char maxLabel[32];
    sprintf_s(maxLabel, "%.0f", labelMaxValue);
    renderer.DrawTextBlock(RenderRect{rect.left, rect.top, rect.left + layout.axisWidth, layout.graphTop},
        maxLabel,
        TextStyleId::Small,
        RenderColorId::MutedText,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));

    const RenderColorId plotColor = RenderColorId::Accent;
    std::vector<PlotPoint> plotPoints;
    plotPoints.reserve(history.size());
    for (size_t i = 0; i < history.size(); ++i) {
        const double valueRatio = ClampFinite(FiniteNonNegativeOr(history[i]) / maxValue, 0.0, 1.0);
        const double sampleX = static_cast<double>(i) - plotShift;
        const double x = static_cast<double>(layout.graphLeft) +
                         (sampleX * static_cast<double>(layout.plotWidth) / static_cast<double>(historyDenominator));
        const double y =
            static_cast<double>(layout.graphBottom) - std::round(valueRatio * static_cast<double>(layout.plotHeight));
        plotPoints.push_back(PlotPoint{x, y});
    }
    const std::vector<RenderPoint> clippedPlotPoints =
        ClipPlotPointsToGraph(plotPoints, layout.graphLeft, layout.graphRight);
    if (clippedPlotPoints.size() >= 2) {
        renderer.DrawPolyline(
            clippedPlotPoints, RenderStroke::Solid(plotColor, static_cast<float>(layout.plotStrokeWidth)));
    }

    if (!history.empty() && layout.leaderDiameter > 0) {
        const double leaderY =
            PlotYAtX(plotPoints, layout.graphRight).value_or(static_cast<double>(layout.graphBottom));
        FillCircle(
            renderer, layout.graphRight, static_cast<int>(std::round(leaderY)), layout.leaderDiameter, plotColor);
    }
}

class ThroughputChartAnimation final : public WidgetAnimation {
public:
    ThroughputChartAnimation(WidgetAnimationLayer layer,
        AnimationDataKey key,
        RenderRect rect,
        ThroughputGraphLayout layout,
        ThroughputChartSample target,
        double timeMarkerIntervalSamples,
        bool drawValues)
        : layer_(layer), key_(std::move(key)), rect_(rect), layout_(layout), target_(std::move(target)),
          timeMarkerIntervalSamples_(timeMarkerIntervalSamples), drawValues_(drawValues) {}

    const AnimationDataKey& Key() const override {
        return key_;
    }

    WidgetAnimationLayer Layer() const override {
        return layer_;
    }

    WidgetAnimationStatePtr TargetState() const override {
        return MakeThroughputChartAnimationState(target_);
    }

    void Draw(Renderer& renderer, const WidgetAnimationState& state) const override {
        const ThroughputChartSample& sample = ThroughputChartSampleFromState(state);
        DrawGraphAnimated(renderer,
            rect_,
            layout_,
            sample.samples,
            sample.maxGraph,
            sample.guideStepMbps,
            sample.timeMarkerOffsetSamples,
            sample.plotShiftSamples,
            timeMarkerIntervalSamples_,
            target_.maxGraph,
            drawValues_);
    }

private:
    WidgetAnimationLayer layer_ = WidgetAnimationLayer::Snapshot;
    AnimationDataKey key_;
    RenderRect rect_{};
    ThroughputGraphLayout layout_{};
    ThroughputChartSample target_;
    double timeMarkerIntervalSamples_ = 20.0;
    bool drawValues_ = true;
};

int EffectiveThroughputPreferredHeight(const WidgetHost& renderer) {
    const int headerHeight = renderer.Renderer().TextMetrics().smallText;
    const int graphLabelHeight = renderer.Renderer().TextMetrics().smallText;
    return headerHeight + std::max(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.throughput.headerGap)) +
           graphLabelHeight;
}

}  // namespace

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
    ThroughputChartSample targetSample;
    targetSample.samples = metric.history;
    targetSample.maxGraph = metric.maxGraph;
    targetSample.timeMarkerOffsetSamples = metric.timeMarkerOffsetSamples;
    targetSample.guideStepMbps = metric.guideStepMbps;
    const bool drawValues = renderer.CurrentRenderMode() != WidgetHost::RenderMode::Blank;
    DrawGraphSnapshot(renderer,
        layout.graphRect,
        layout,
        targetSample.maxGraph,
        renderer.MakeEditableTextBinding(
            widget, WidgetHost::LayoutEditParameter::FontSmall, 2, renderer.Config().layout.fonts.smallText.size));
    renderer.AddWidgetAnimation(std::make_unique<ThroughputChartAnimation>(renderer.CurrentWidgetAnimationLayer(),
        AnimationDataKey{metric_, {}},
        layout.graphRect,
        layout,
        targetSample,
        metric.timeMarkerIntervalSamples,
        drawValues));
}

void ThroughputWidget::BuildStaticAnchors(WidgetHost& renderer, const WidgetLayout& widget) const {
    const ThroughputGraphLayout& layout = layoutState_;
    renderer.EditArtifacts().RegisterStaticEditAnchor(LayoutEditAnchorRegistration{
        .key = LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::ThroughputLeaderDiameter,
            0},
        .targetRect = layout.leaderAnchorRect,
        .anchorRect = layout.leaderAnchorRect,
        .shape = AnchorShape::Circle,
        .value = renderer.Config().layout.throughput.leaderDiameter,
        .drag = LayoutEditAnchorDrag::RadialDistance(
            RenderPoint{layout.leaderAnchorCenterX, layout.leaderAnchorCenterY}, 2.0),
        .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
        .targetOutline = LayoutEditTargetOutline::Hidden});

    renderer.EditArtifacts().RegisterStaticEditAnchor(LayoutEditAnchorRegistration{
        .key = LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::ThroughputPlotStrokeWidth,
            0},
        .targetRect = layout.plotAnchorRect,
        .anchorRect = layout.plotAnchorRect,
        .shape = AnchorShape::Circle,
        .value = renderer.Config().layout.throughput.plotStrokeWidth,
        .drag =
            LayoutEditAnchorDrag::RadialDistance(RenderPoint{layout.plotAnchorCenterX, layout.plotAnchorCenterY}, 2.0),
        .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
        .targetOutline = LayoutEditTargetOutline::Hidden});

    renderer.EditArtifacts().RegisterStaticEditAnchor(LayoutEditAnchorRegistration{
        .key = LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::ThroughputGuideStrokeWidth,
            0},
        .targetRect = layout.guideAnchorRect,
        .anchorRect = layout.guideAnchorRect,
        .shape = AnchorShape::Circle,
        .value = renderer.Config().layout.throughput.guideStrokeWidth,
        .drag = LayoutEditAnchorDrag::RadialDistance(RenderPoint{layout.guideCenterX, layout.guideCenterY}, 2.0),
        .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
        .targetOutline = LayoutEditTargetOutline::Hidden});

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
