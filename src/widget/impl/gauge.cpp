#include "widget/impl/gauge.h"

#include <algorithm>
#include <cmath>

#include "telemetry/metrics.h"
#include "util/numeric_safety.h"
#include "widget/widget_host.h"

struct GaugeSharedLayout {
    int radius = 0;
};

namespace {

using GaugeSegmentLayout = GaugeWidget::SegmentLayout;

double MinimumGaugeSegmentSweep(double totalSweep, int segmentCount) {
    if (totalSweep <= 0.0 || segmentCount <= 0) {
        return 0.0;
    }
    return (std::min)(0.25, totalSweep / static_cast<double>(segmentCount));
}

GaugeSegmentLayout ComputeGaugeSegmentLayout(
    double requestedSweep, int requestedSegmentCount, double requestedSegmentGap) {
    GaugeSegmentLayout layout;
    layout.segmentCount = (std::max)(1, requestedSegmentCount);
    layout.totalSweep = std::clamp(requestedSweep, 0.0, 360.0);
    layout.gapSweep = (std::max)(0.0, 360.0 - layout.totalSweep);
    layout.gaugeStart = 90.0 + (layout.gapSweep / 2.0);
    layout.gaugeEnd = layout.gaugeStart + layout.totalSweep;

    if (layout.segmentCount <= 1) {
        layout.segmentGap = 0.0;
        layout.segmentSweep = layout.totalSweep;
        layout.pitchSweep = layout.totalSweep;
        layout.maxSegmentSweep = layout.totalSweep;
        return layout;
    }

    layout.maxSegmentSweep = layout.totalSweep / static_cast<double>(layout.segmentCount);
    const double minSegmentSweep = MinimumGaugeSegmentSweep(layout.totalSweep, layout.segmentCount);
    const double maxSegmentGap = (std::max)(0.0,
        (layout.totalSweep - (minSegmentSweep * static_cast<double>(layout.segmentCount))) /
            static_cast<double>(layout.segmentCount - 1));
    layout.segmentGap = std::clamp(requestedSegmentGap, 0.0, maxSegmentGap);
    layout.segmentSweep = (std::max)(minSegmentSweep,
        (layout.totalSweep - (layout.segmentGap * static_cast<double>(layout.segmentCount - 1))) /
            static_cast<double>(layout.segmentCount));
    layout.pitchSweep = layout.segmentSweep + layout.segmentGap;
    return layout;
}

RenderPoint PolarPoint(int cx, int cy, int radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return RenderPoint{cx + static_cast<int>(std::lround(std::cos(radians) * static_cast<double>(radius))),
        cy + static_cast<int>(std::lround(std::sin(radians) * static_cast<double>(radius)))};
}

RenderRect ExpandSegmentBounds(RenderPoint start, RenderPoint end, int inset) {
    return RenderRect{((std::min))(start.x, end.x) - inset,
        ((std::min))(start.y, end.y) - inset,
        ((std::max))(start.x, end.x) + inset + 1,
        ((std::max))(start.y, end.y) + inset + 1};
}

double NormalizeAngle(double angleDegrees) {
    double normalized = std::fmod(angleDegrees, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    return normalized;
}

bool AngleInSweep(double angleDegrees, double startAngleDegrees, double sweepAngleDegrees) {
    if (std::abs(sweepAngleDegrees) >= 360.0) {
        return true;
    }
    const double angle = NormalizeAngle(angleDegrees);
    const double start = NormalizeAngle(startAngleDegrees);
    if (sweepAngleDegrees >= 0.0) {
        const double delta = NormalizeAngle(angle - start);
        return delta <= sweepAngleDegrees + 0.0001;
    }
    const double delta = NormalizeAngle(start - angle);
    return delta <= -sweepAngleDegrees + 0.0001;
}

struct RenderPointF {
    double x = 0.0;
    double y = 0.0;
};

RenderPointF RenderArcPoint(RenderPoint center, int radiusX, int radiusY, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return RenderPointF{static_cast<double>(center.x) + std::cos(radians) * static_cast<double>(radiusX),
        static_cast<double>(center.y) + std::sin(radians) * static_cast<double>(radiusY)};
}

RenderRect MakeCircleAnchorRect(int centerX, int centerY, int representedDiameter, int extraDiameter) {
    const int diameter = (std::max)(4, representedDiameter + extraDiameter);
    const int radius = diameter / 2;
    return RenderRect{centerX - radius, centerY - radius, centerX - radius + diameter, centerY - radius + diameter};
}

RenderArc MakeRingSegmentArc(
    int centerX, int centerY, int outerRadius, int thickness, double startAngleDegrees, double sweepAngleDegrees) {
    RenderArc arc;
    const int innerRadius = (std::max)(0, outerRadius - thickness);
    if (outerRadius <= 0 || thickness <= 0 || innerRadius >= outerRadius || sweepAngleDegrees <= 0.0) {
        return arc;
    }
    const int radius = (std::max)(1, outerRadius - (thickness / 2));
    return RenderArc{RenderPoint{centerX, centerY}, radius, radius, startAngleDegrees, sweepAngleDegrees};
}

RenderRect ComputeGaugeSegmentBounds(
    int centerX, int centerY, int outerRadius, int thickness, double startAngleDegrees, double sweepAngleDegrees) {
    const int innerRadius = (std::max)(0, outerRadius - thickness);
    if (outerRadius <= 0 || thickness <= 0 || innerRadius >= outerRadius || sweepAngleDegrees <= 0.0) {
        return {};
    }

    const RenderPoint center{centerX, centerY};
    std::vector<RenderPointF> points;
    points.reserve(10);
    points.push_back(RenderArcPoint(center, outerRadius, outerRadius, startAngleDegrees));
    points.push_back(RenderArcPoint(center, outerRadius, outerRadius, startAngleDegrees + sweepAngleDegrees));
    points.push_back(RenderArcPoint(center, innerRadius, innerRadius, startAngleDegrees));
    points.push_back(RenderArcPoint(center, innerRadius, innerRadius, startAngleDegrees + sweepAngleDegrees));
    for (double cardinal = 0.0; cardinal < 360.0; cardinal += 90.0) {
        if (AngleInSweep(cardinal, startAngleDegrees, sweepAngleDegrees)) {
            points.push_back(RenderArcPoint(center, outerRadius, outerRadius, cardinal));
        }
    }

    double left = points.front().x;
    double top = points.front().y;
    double right = points.front().x;
    double bottom = points.front().y;
    for (const RenderPointF point : points) {
        left = (std::min)(left, point.x);
        top = (std::min)(top, point.y);
        right = (std::max)(right, point.x);
        bottom = (std::max)(bottom, point.y);
    }
    return RenderRect{static_cast<int>(std::floor(left)),
        static_cast<int>(std::floor(top)),
        static_cast<int>(std::ceil(right)),
        static_cast<int>(std::ceil(bottom))};
}

int GaugeOuterRadiusForRect(const WidgetHost& renderer, const RenderRect& rect) {
    const int width = (std::max)(0, rect.right - rect.left);
    const int height = (std::max)(0, rect.bottom - rect.top);
    const int outerPadding =
        (std::max)(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.gauge.outerPadding));
    return (std::max)(1, ((std::min)(width, height) / 2) - outerPadding);
}

int GaugeTextHalfWidth(const WidgetHost& renderer, const std::string& metricRef) {
    const std::string& sampleValueText = renderer.ResolveConfiguredMetricSampleValueText(metricRef);
    const MetricDefinitionConfig* definition = renderer.FindConfiguredMetricDefinition(metricRef);
    const std::string_view valueText =
        sampleValueText.empty() ? std::string_view("100%") : std::string_view(sampleValueText);
    const int valueWidth = renderer.Renderer().MeasureTextWidth(TextStyleId::Big, valueText);
    const std::string_view labelText = definition != nullptr ? std::string_view(definition->label) : std::string_view{};
    const int labelWidth = renderer.Renderer().MeasureTextWidth(TextStyleId::Small, labelText);
    return (std::max)(1, (((std::max)(valueWidth, labelWidth) + 1) / 2));
}

int EffectiveGaugePreferredRadius(const WidgetHost& renderer, const std::string& metricRef) {
    const int outerPadding =
        (std::max)(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.gauge.outerPadding));
    const int ringThickness =
        (std::max)(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.gauge.ringThickness));
    const int halfWidth = GaugeTextHalfWidth(renderer, metricRef);
    const int valueHalfHeight = (std::max)(1, (renderer.Renderer().TextMetrics().big + 1) / 2);
    const int labelHalfHeight = (std::max)(1, (renderer.Renderer().TextMetrics().smallText + 1) / 2);
    const int innerRadius = (std::max)(halfWidth, (std::max)(valueHalfHeight, labelHalfHeight));
    const int outerRadius = innerRadius + ringThickness + outerPadding;
    return (std::max)(1, outerRadius);
}

}  // namespace

WidgetClass GaugeWidget::Class() const {
    return WidgetClass::Gauge;
}

std::unique_ptr<Widget> GaugeWidget::Clone() const {
    return std::make_unique<GaugeWidget>(*this);
}

void GaugeWidget::Initialize(const LayoutNodeConfig& node) {
    metric_ = node.parameter;
    sharedLayout_.reset();
}

int GaugeWidget::PreferredHeight(const WidgetHost& renderer) const {
    return EffectiveGaugePreferredRadius(renderer, metric_) * 2;
}

void GaugeWidget::ResolveLayoutState(const WidgetHost& renderer, const RenderRect& rect) {
    layoutState_ = {};
    layoutState_.outerRadius = sharedLayout_ != nullptr && sharedLayout_->radius > 0
                                   ? sharedLayout_->radius
                                   : GaugeOuterRadiusForRect(renderer, rect);
    layoutState_.cx = rect.left + ((std::max)(0, rect.right - rect.left) / 2);
    layoutState_.cy = rect.top + ((std::max)(0, rect.bottom - rect.top) / 2);
    layoutState_.ringThickness =
        (std::max)(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.gauge.ringThickness));
    layoutState_.segmentLayout = ComputeGaugeSegmentLayout(renderer.Config().layout.gauge.sweepDegrees,
        renderer.Config().layout.gauge.segmentCount,
        renderer.Config().layout.gauge.segmentGapDegrees);
    layoutState_.innerRadius = (std::max)(0, layoutState_.outerRadius - layoutState_.ringThickness);
    layoutState_.anchorPadding = (std::max)(1, renderer.Renderer().ScaleLogical(1));
    layoutState_.anchorSize = (std::max)(4, renderer.Renderer().ScaleLogical(6));
    layoutState_.anchorHalf = layoutState_.anchorSize / 2;
    layoutState_.halfWidth = GaugeTextHalfWidth(renderer, metric_);
    layoutState_.valueBottom = renderer.Renderer().ScaleLogical(renderer.Config().layout.gauge.valueBottom);
    layoutState_.valueHeight = renderer.Renderer().TextMetrics().big;
    layoutState_.labelBottom = renderer.Renderer().ScaleLogical(renderer.Config().layout.gauge.labelBottom);
    layoutState_.labelHeight = renderer.Renderer().TextMetrics().smallText;
    layoutState_.guideHalfExtension = (std::max)(1, layoutState_.ringThickness / 2);
    layoutState_.hitInset = (std::max)(4, renderer.Renderer().ScaleLogical(5));
    layoutState_.segmentCountAnchorRect = RenderRect{layoutState_.cx - layoutState_.anchorHalf,
        layoutState_.cy - layoutState_.outerRadius - layoutState_.anchorHalf,
        layoutState_.cx - layoutState_.anchorHalf + layoutState_.anchorSize,
        layoutState_.cy - layoutState_.outerRadius - layoutState_.anchorHalf + layoutState_.anchorSize};
    layoutState_.outerPaddingAnchorRect = MakeCircleAnchorRect(
        layoutState_.cx, layoutState_.cy, layoutState_.outerRadius * 2, layoutState_.anchorPadding);
    layoutState_.ringThicknessAnchorRect = MakeCircleAnchorRect(
        layoutState_.cx, layoutState_.cy, layoutState_.innerRadius * 2, layoutState_.anchorPadding);
    layoutState_.valueRect = RenderRect{layoutState_.cx - layoutState_.halfWidth,
        layoutState_.cy + layoutState_.valueBottom - layoutState_.valueHeight,
        layoutState_.cx + layoutState_.halfWidth,
        layoutState_.cy + layoutState_.valueBottom};
    layoutState_.labelRect = RenderRect{layoutState_.cx - layoutState_.halfWidth,
        layoutState_.cy + layoutState_.labelBottom - layoutState_.labelHeight,
        layoutState_.cx + layoutState_.halfWidth,
        layoutState_.cy + layoutState_.labelBottom};
    layoutState_.ringSegments.clear();
    layoutState_.ringSegmentBounds.clear();
    layoutState_.ringSegments.reserve(static_cast<size_t>(layoutState_.segmentLayout.segmentCount));
    layoutState_.ringSegmentBounds.reserve(static_cast<size_t>(layoutState_.segmentLayout.segmentCount));
    for (int i = 0; i < layoutState_.segmentLayout.segmentCount; ++i) {
        const double slotStart =
            layoutState_.segmentLayout.gaugeStart + layoutState_.segmentLayout.pitchSweep * static_cast<double>(i);
        layoutState_.ringSegments.push_back(MakeRingSegmentArc(layoutState_.cx,
            layoutState_.cy,
            layoutState_.outerRadius,
            layoutState_.ringThickness,
            slotStart,
            layoutState_.segmentLayout.segmentSweep));
        layoutState_.ringSegmentBounds.push_back(ComputeGaugeSegmentBounds(layoutState_.cx,
            layoutState_.cy,
            layoutState_.outerRadius,
            layoutState_.ringThickness,
            slotStart,
            layoutState_.segmentLayout.segmentSweep));
    }
}

void GaugeWidget::Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const {
    const MetricValue& metric = metrics.ResolveMetric(metric_);
    const GaugeSegmentLayout& gaugeLayout = layoutState_.segmentLayout;
    const double clampedRatio = ClampFinite(metric.ratio, 0.0, 1.0);
    const int filledSegments =
        clampedRatio <= 0.0
            ? 0
            : std::clamp(static_cast<int>(std::ceil(clampedRatio * static_cast<double>(gaugeLayout.segmentCount))),
                  1,
                  gaugeLayout.segmentCount);
    const double clampedPeakRatio = ClampFinite(metric.peakRatio, 0.0, 1.0);
    const int peakSegment =
        clampedPeakRatio <= 0.0
            ? -1
            : std::clamp(
                  static_cast<int>(std::ceil(clampedPeakRatio * static_cast<double>(gaugeLayout.segmentCount))) - 1,
                  0,
                  gaugeLayout.segmentCount - 1);

    const RenderStroke trackStroke =
        RenderStroke::Solid(RenderColorId::Track, static_cast<float>(layoutState_.ringThickness));
    renderer.Renderer().DrawArcs(layoutState_.ringSegments, trackStroke);
    if (renderer.CurrentRenderMode() != WidgetHost::RenderMode::Blank && filledSegments > 0) {
        const RenderStroke accentStroke =
            RenderStroke::Solid(RenderColorId::Accent, static_cast<float>(layoutState_.ringThickness));
        renderer.Renderer().DrawArcs(
            std::span<const RenderArc>(layoutState_.ringSegments.data(), static_cast<size_t>(filledSegments)),
            accentStroke);
    }
    if (renderer.CurrentRenderMode() != WidgetHost::RenderMode::Blank && peakSegment >= 0 &&
        static_cast<size_t>(peakSegment) < layoutState_.ringSegments.size()) {
        const size_t peakSegmentIndex = static_cast<size_t>(peakSegment);
        renderer.Renderer().DrawArc(layoutState_.ringSegments[peakSegmentIndex],
            RenderStroke::Solid(RenderColorId::PeakGhost, static_cast<float>(layoutState_.ringThickness)));
        if (peakSegmentIndex < layoutState_.ringSegmentBounds.size() &&
            !layoutState_.ringSegmentBounds[peakSegmentIndex].IsEmpty()) {
            renderer.EditArtifacts().RegisterDynamicColorEditRegion(
                WidgetHost::LayoutEditParameter::ColorPeakGhost, layoutState_.ringSegmentBounds[peakSegmentIndex]);
        }
    }

    if (renderer.CurrentRenderMode() != WidgetHost::RenderMode::Blank) {
        const WidgetHost::TextLayoutResult valueLayout = renderer.Renderer().DrawTextBlock(layoutState_.valueRect,
            metric.valueText,
            TextStyleId::Big,
            RenderColorId::Foreground,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
        renderer.EditArtifacts().RegisterDynamicTextAnchor(valueLayout,
            renderer.MakeEditableTextBinding(
                widget, WidgetHost::LayoutEditParameter::FontBig, 0, renderer.Config().layout.fonts.big.size),
            WidgetHost::LayoutEditParameter::ColorForeground);
        renderer.EditArtifacts().RegisterDynamicTextAnchor(
            valueLayout, renderer.MakeMetricTextBinding(widget, metric_, 100));
    }
    const RenderRect ringBounds{layoutState_.cx - layoutState_.outerRadius,
        layoutState_.cy - layoutState_.outerRadius,
        layoutState_.cx + layoutState_.outerRadius,
        layoutState_.cy + layoutState_.outerRadius};
    renderer.EditArtifacts().RegisterDynamicColorEditRegion(WidgetHost::LayoutEditParameter::ColorAccent,
        RenderRect{ringBounds.left, ringBounds.top, layoutState_.cx, ringBounds.bottom});
    renderer.EditArtifacts().RegisterDynamicColorEditRegion(WidgetHost::LayoutEditParameter::ColorTrack,
        RenderRect{layoutState_.cx, ringBounds.top, ringBounds.right, ringBounds.bottom});
    renderer.Renderer().DrawText(layoutState_.labelRect,
        metric.label,
        TextStyleId::Small,
        RenderColorId::MutedText,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
}

void GaugeWidget::BuildStaticAnchors(WidgetHost& renderer, const WidgetLayout& widget) const {
    const int cx = layoutState_.cx;
    const int cy = layoutState_.cy;
    const int outerRadius = layoutState_.outerRadius;
    renderer.EditArtifacts().RegisterStaticEditAnchor(LayoutEditAnchorRegistration{
        .key = LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::GaugeSegmentCount,
            0},
        .targetRect = widget.rect,
        .anchorRect = layoutState_.segmentCountAnchorRect,
        .shape = AnchorShape::Diamond,
        .value = renderer.Config().layout.gauge.segmentCount,
        .drag = LayoutEditAnchorDrag::AxisDelta(AnchorDragAxis::Both, RenderPoint{cx, cy - outerRadius}),
        .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered});
    renderer.EditArtifacts().RegisterStaticEditAnchor(LayoutEditAnchorRegistration{
        .key = LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::GaugeOuterPadding,
            0},
        .targetRect = layoutState_.outerPaddingAnchorRect,
        .anchorRect = layoutState_.outerPaddingAnchorRect,
        .shape = AnchorShape::Circle,
        .value = renderer.Config().layout.gauge.outerPadding,
        .drag = LayoutEditAnchorDrag::RadialDistance(RenderPoint{cx, cy}, -1.0),
        .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
        .targetOutline = LayoutEditTargetOutline::Hidden});
    renderer.EditArtifacts().RegisterStaticEditAnchor(LayoutEditAnchorRegistration{
        .key = LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            WidgetHost::LayoutEditParameter::GaugeRingThickness,
            0},
        .targetRect = layoutState_.ringThicknessAnchorRect,
        .anchorRect = layoutState_.ringThicknessAnchorRect,
        .shape = AnchorShape::Circle,
        .value = renderer.Config().layout.gauge.ringThickness,
        .drag = LayoutEditAnchorDrag::RadialDistance(RenderPoint{cx, cy}, -1.0),
        .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
        .targetOutline = LayoutEditTargetOutline::Hidden});
    const MetricDefinitionConfig* definition = renderer.FindConfiguredMetricDefinition(metric_);
    if (definition != nullptr && !definition->label.empty()) {
        renderer.EditArtifacts().RegisterStaticTextAnchor(layoutState_.labelRect,
            definition->label,
            TextStyleId::Small,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center),
            renderer.MakeEditableTextBinding(
                widget, WidgetHost::LayoutEditParameter::FontSmall, 1, renderer.Config().layout.fonts.smallText.size),
            WidgetHost::LayoutEditParameter::ColorMutedText);
        renderer.EditArtifacts().RegisterStaticTextAnchor(layoutState_.labelRect,
            definition->label,
            TextStyleId::Small,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center),
            renderer.MakeMetricTextBinding(widget, metric_, 101));
    }
}

void GaugeWidget::BuildEditGuides(WidgetHost& renderer, const WidgetLayout& widget) const {
    const int outerRadius = layoutState_.outerRadius;
    if (outerRadius <= 0) {
        return;
    }

    const int cx = layoutState_.cx;
    const int cy = layoutState_.cy;
    const GaugeSegmentLayout& gaugeLayout = layoutState_.segmentLayout;
    const int ringThickness = layoutState_.ringThickness;
    const int guideHalfExtension = layoutState_.guideHalfExtension;
    const int hitInset = layoutState_.hitInset;
    const int halfWidth = layoutState_.halfWidth;

    const auto addRadialGuide = [&](WidgetHost::LayoutEditParameter parameter,
                                    int guideId,
                                    double angleDegrees,
                                    double value,
                                    double angularMin,
                                    double angularMax) {
        const int innerGuideRadius = (std::max)(1, outerRadius - ringThickness - guideHalfExtension);
        const int outerGuideRadius = outerRadius + guideHalfExtension;
        const RenderPoint guideStart = PolarPoint(cx, cy, innerGuideRadius, angleDegrees);
        const RenderPoint guideEnd = PolarPoint(cx, cy, outerGuideRadius, angleDegrees);
        LayoutEditWidgetGuide guide;
        guide.axis = LayoutGuideAxis::Vertical;
        guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.drawStart = guideStart;
        guide.drawEnd = guideEnd;
        guide.hitRect = ExpandSegmentBounds(guideStart, guideEnd, hitInset);
        guide.dragOrigin = RenderPoint{cx, cy};
        guide.value = value;
        guide.angularDrag = true;
        guide.angularMin = angularMin;
        guide.angularMax = angularMax;
        renderer.EditArtifacts().RegisterWidgetEditGuide(std::move(guide));
    };

    addRadialGuide(WidgetHost::LayoutEditParameter::GaugeSweepDegrees,
        0,
        gaugeLayout.gaugeEnd,
        gaugeLayout.totalSweep,
        0.0,
        360.0);
    addRadialGuide(WidgetHost::LayoutEditParameter::GaugeSegmentGapDegrees,
        gaugeLayout.segmentCount,
        gaugeLayout.gaugeStart + gaugeLayout.segmentSweep,
        gaugeLayout.segmentGap,
        gaugeLayout.gaugeStart,
        gaugeLayout.gaugeStart + gaugeLayout.maxSegmentSweep);

    const auto addHorizontalGuide = [&](WidgetHost::LayoutEditParameter parameter, int guideId, int bottomOffset) {
        const int y = cy + renderer.Renderer().ScaleLogical(bottomOffset);
        LayoutEditWidgetGuide guide;
        guide.axis = LayoutGuideAxis::Horizontal;
        guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.drawStart = RenderPoint{cx - halfWidth, y};
        guide.drawEnd = RenderPoint{cx + halfWidth, y};
        guide.hitRect = RenderRect{cx - halfWidth, y - hitInset, cx + halfWidth, y + hitInset + 1};
        guide.value = bottomOffset;
        guide.dragDirection = 1;
        renderer.EditArtifacts().RegisterWidgetEditGuide(std::move(guide));
    };

    addHorizontalGuide(
        WidgetHost::LayoutEditParameter::GaugeValueBottom, 100, renderer.Config().layout.gauge.valueBottom);
    addHorizontalGuide(
        WidgetHost::LayoutEditParameter::GaugeLabelBottom, 101, renderer.Config().layout.gauge.labelBottom);
}

void GaugeWidget::FinalizeLayoutGroup(WidgetHost& renderer, const std::vector<WidgetLayout*>& widgets) {
    auto sharedLayout = std::make_shared<GaugeSharedLayout>();
    int gaugeCount = 0;
    for (WidgetLayout* widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        GaugeWidget* gauge = dynamic_cast<GaugeWidget*>(widget->widget.get());
        if (gauge == nullptr) {
            continue;
        }
        const int gaugeRadius = GaugeOuterRadiusForRect(renderer, widget->rect);
        sharedLayout->radius = gaugeCount == 0 ? gaugeRadius : (std::min)(sharedLayout->radius, gaugeRadius);
        ++gaugeCount;
    }

    for (WidgetLayout* widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        GaugeWidget* gauge = dynamic_cast<GaugeWidget*>(widget->widget.get());
        if (gauge != nullptr) {
            gauge->sharedLayout_ = sharedLayout;
        }
    }
}
