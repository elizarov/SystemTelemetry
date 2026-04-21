#include "widget/gauge.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "dashboard_metrics.h"
#include "dashboard_renderer.h"
#include "numeric_safety.h"

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

D2D1_ARC_SIZE GaugeArcSize(double sweepAngleDegrees) {
    return std::abs(sweepAngleDegrees) > 180.0 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
}

Microsoft::WRL::ComPtr<ID2D1GeometryGroup> BuildCombinedD2DGaugePath(const DashboardRenderer& renderer,
    const std::vector<Microsoft::WRL::ComPtr<ID2D1PathGeometry>>& segmentPaths,
    size_t segmentCount) {
    return renderer.CreateD2DGeometryGroup(segmentPaths, segmentCount);
}

Microsoft::WRL::ComPtr<ID2D1PathGeometry> BuildD2DGaugeSegmentPath(const DashboardRenderer& renderer,
    float cx,
    float cy,
    float outerRadius,
    float thickness,
    double startAngleDegrees,
    double sweepAngleDegrees) {
    if (outerRadius <= 0.0f || thickness <= 0.0f || sweepAngleDegrees <= 0.0) {
        return {};
    }

    const float innerRadius = (std::max)(0.0f, outerRadius - thickness);
    if (outerRadius <= innerRadius) {
        return {};
    }

    const double startRadians = startAngleDegrees * 3.14159265358979323846 / 180.0;
    const double endRadians = (startAngleDegrees + sweepAngleDegrees) * 3.14159265358979323846 / 180.0;
    const D2D1_POINT_2F outerStart = D2D1::Point2F(cx + static_cast<float>(std::cos(startRadians) * outerRadius),
        cy + static_cast<float>(std::sin(startRadians) * outerRadius));
    const D2D1_POINT_2F outerEnd = D2D1::Point2F(cx + static_cast<float>(std::cos(endRadians) * outerRadius),
        cy + static_cast<float>(std::sin(endRadians) * outerRadius));
    const D2D1_POINT_2F innerEnd = D2D1::Point2F(cx + static_cast<float>(std::cos(endRadians) * innerRadius),
        cy + static_cast<float>(std::sin(endRadians) * innerRadius));
    const D2D1_POINT_2F innerStart = D2D1::Point2F(cx + static_cast<float>(std::cos(startRadians) * innerRadius),
        cy + static_cast<float>(std::sin(startRadians) * innerRadius));

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path = renderer.CreateD2DPathGeometry();
    if (path == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(sink.GetAddressOf())) || sink == nullptr) {
        return {};
    }
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(outerStart, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc(D2D1::ArcSegment(outerEnd,
        D2D1::SizeF(outerRadius, outerRadius),
        0.0f,
        D2D1_SWEEP_DIRECTION_CLOCKWISE,
        GaugeArcSize(sweepAngleDegrees)));
    sink->AddLine(innerEnd);
    if (innerRadius > 0.0f) {
        sink->AddArc(D2D1::ArcSegment(innerStart,
            D2D1::SizeF(innerRadius, innerRadius),
            0.0f,
            D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
            GaugeArcSize(sweepAngleDegrees)));
    } else {
        sink->AddLine(D2D1::Point2F(cx, cy));
    }
    sink->AddLine(outerStart);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) {
        return {};
    }
    return path;
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

RenderRect MakeCircleAnchorRect(int centerX, int centerY, int representedDiameter, int extraDiameter) {
    const int diameter = (std::max)(4, representedDiameter + extraDiameter);
    const int radius = diameter / 2;
    return RenderRect{centerX - radius, centerY - radius, centerX - radius + diameter, centerY - radius + diameter};
}

std::optional<RenderRect> GaugeSegmentBounds(ID2D1Geometry* segmentPath) {
    if (segmentPath == nullptr) {
        return std::nullopt;
    }
    D2D1_RECT_F bounds{};
    if (FAILED(segmentPath->GetBounds(nullptr, &bounds))) {
        return std::nullopt;
    }
    return RenderRect{static_cast<LONG>(std::floor(bounds.left)),
        static_cast<LONG>(std::floor(bounds.top)),
        static_cast<LONG>(std::ceil(bounds.right)),
        static_cast<LONG>(std::ceil(bounds.bottom))};
}

int GaugeOuterRadiusForRect(const DashboardRenderer& renderer, const RenderRect& rect) {
    const int width = (std::max)(0, rect.right - rect.left);
    const int height = (std::max)(0, rect.bottom - rect.top);
    const int outerPadding = (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.gauge.outerPadding));
    return (std::max)(1, ((std::min)(width, height) / 2) - outerPadding);
}

int GaugeTextHalfWidth(const DashboardRenderer& renderer, const std::string& metricRef) {
    const std::string& sampleValueText = renderer.ResolveConfiguredMetricSampleValueText(metricRef);
    const MetricDefinitionConfig* definition = renderer.FindConfiguredMetricDefinition(metricRef);
    const std::string_view valueText =
        sampleValueText.empty() ? std::string_view("100%") : std::string_view(sampleValueText);
    const int valueWidth = renderer.MeasureTextWidth(TextStyleId::Big, valueText);
    const std::string_view labelText = definition != nullptr ? std::string_view(definition->label) : std::string_view{};
    const int labelWidth = renderer.MeasureTextWidth(TextStyleId::Small, labelText);
    return (std::max)(1, (((std::max)(valueWidth, labelWidth) + 1) / 2));
}

int EffectiveGaugePreferredRadius(const DashboardRenderer& renderer, const std::string& metricRef) {
    const int outerPadding = (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.gauge.outerPadding));
    const int ringThickness = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.gauge.ringThickness));
    const int halfWidth = GaugeTextHalfWidth(renderer, metricRef);
    const int valueHalfHeight = (std::max)(1, (renderer.TextMetrics().big + 1) / 2);
    const int labelHalfHeight = (std::max)(1, (renderer.TextMetrics().smallText + 1) / 2);
    const int innerRadius = (std::max)(halfWidth, (std::max)(valueHalfHeight, labelHalfHeight));
    const int outerRadius = innerRadius + ringThickness + outerPadding;
    return (std::max)(1, outerRadius);
}

}  // namespace

DashboardWidgetClass GaugeWidget::Class() const {
    return DashboardWidgetClass::Gauge;
}

std::unique_ptr<DashboardWidget> GaugeWidget::Clone() const {
    return std::make_unique<GaugeWidget>(*this);
}

void GaugeWidget::Initialize(const LayoutNodeConfig& node) {
    metric_ = node.parameter;
    sharedLayout_.reset();
}

int GaugeWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return EffectiveGaugePreferredRadius(renderer, metric_) * 2;
}

void GaugeWidget::ResolveLayoutState(const DashboardRenderer& renderer, const RenderRect& rect) {
    layoutState_ = {};
    layoutState_.outerRadius = sharedLayout_ != nullptr && sharedLayout_->radius > 0
                                   ? sharedLayout_->radius
                                   : GaugeOuterRadiusForRect(renderer, rect);
    layoutState_.cx = rect.left + ((std::max)(0, rect.right - rect.left) / 2);
    layoutState_.cy = rect.top + ((std::max)(0, rect.bottom - rect.top) / 2);
    layoutState_.ringThickness = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.gauge.ringThickness));
    layoutState_.segmentLayout = ComputeGaugeSegmentLayout(renderer.Config().layout.gauge.sweepDegrees,
        renderer.Config().layout.gauge.segmentCount,
        renderer.Config().layout.gauge.segmentGapDegrees);
    layoutState_.innerRadius = (std::max)(0, layoutState_.outerRadius - layoutState_.ringThickness);
    layoutState_.anchorPadding = (std::max)(1, renderer.ScaleLogical(1));
    layoutState_.anchorSize = (std::max)(4, renderer.ScaleLogical(6));
    layoutState_.anchorHalf = layoutState_.anchorSize / 2;
    layoutState_.halfWidth = GaugeTextHalfWidth(renderer, metric_);
    layoutState_.valueBottom = renderer.ScaleLogical(renderer.Config().layout.gauge.valueBottom);
    layoutState_.valueHeight = renderer.TextMetrics().big;
    layoutState_.labelBottom = renderer.ScaleLogical(renderer.Config().layout.gauge.labelBottom);
    layoutState_.labelHeight = renderer.TextMetrics().smallText;
    layoutState_.guideHalfExtension = (std::max)(1, layoutState_.ringThickness / 2);
    layoutState_.hitInset = (std::max)(4, renderer.ScaleLogical(5));
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
    layoutState_.d2dSegmentPaths.clear();
    layoutState_.d2dSegmentPaths.reserve(static_cast<size_t>(layoutState_.segmentLayout.segmentCount));
    for (int i = 0; i < layoutState_.segmentLayout.segmentCount; ++i) {
        const double slotStart =
            layoutState_.segmentLayout.gaugeStart + layoutState_.segmentLayout.pitchSweep * static_cast<double>(i);
        layoutState_.d2dSegmentPaths.push_back(BuildD2DGaugeSegmentPath(renderer,
            static_cast<float>(layoutState_.cx),
            static_cast<float>(layoutState_.cy),
            static_cast<float>(layoutState_.outerRadius),
            static_cast<float>(layoutState_.ringThickness),
            slotStart,
            layoutState_.segmentLayout.segmentSweep));
    }
    layoutState_.d2dTrackPath =
        BuildCombinedD2DGaugePath(renderer, layoutState_.d2dSegmentPaths, layoutState_.d2dSegmentPaths.size());
    layoutState_.cachedUsageSegmentCount = -1;
    layoutState_.d2dCachedUsagePath.Reset();
}

void GaugeWidget::Draw(
    DashboardRenderer& renderer, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    const DashboardMetricValue& metric = metrics.ResolveMetric(metric_);
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

    if (layoutState_.d2dTrackPath != nullptr) {
        renderer.FillD2DGeometry(layoutState_.d2dTrackPath.Get(), RenderColorId::Track);
    }
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank && filledSegments > 0) {
        if (layoutState_.cachedUsageSegmentCount != filledSegments || layoutState_.d2dCachedUsagePath == nullptr) {
            layoutState_.d2dCachedUsagePath =
                BuildCombinedD2DGaugePath(renderer, layoutState_.d2dSegmentPaths, static_cast<size_t>(filledSegments));
            layoutState_.cachedUsageSegmentCount = filledSegments;
        }
        if (layoutState_.d2dCachedUsagePath != nullptr) {
            renderer.FillD2DGeometry(layoutState_.d2dCachedUsagePath.Get(), RenderColorId::Accent);
        }
    }
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank && peakSegment >= 0 &&
        static_cast<size_t>(peakSegment) < layoutState_.d2dSegmentPaths.size() &&
        layoutState_.d2dSegmentPaths[static_cast<size_t>(peakSegment)] != nullptr) {
        ID2D1Geometry* peakSegmentPath = layoutState_.d2dSegmentPaths[static_cast<size_t>(peakSegment)].Get();
        renderer.FillD2DGeometry(peakSegmentPath, RenderColorId::PeakGhost);
        if (const auto peakSegmentBounds = GaugeSegmentBounds(peakSegmentPath); peakSegmentBounds.has_value()) {
            renderer.RegisterDynamicColorEditRegion(
                DashboardRenderer::LayoutEditParameter::ColorPeakGhost, *peakSegmentBounds);
        }
    }

    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
        const DashboardRenderer::TextLayoutResult valueLayout = renderer.DrawTextBlock(layoutState_.valueRect,
            metric.valueText,
            TextStyleId::Big,
            RenderColorId::Foreground,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
        renderer.RegisterDynamicTextAnchor(valueLayout,
            renderer.MakeEditableTextBinding(
                widget, DashboardRenderer::LayoutEditParameter::FontBig, 0, renderer.Config().layout.fonts.big.size),
            DashboardRenderer::LayoutEditParameter::ColorForeground);
        renderer.RegisterDynamicTextAnchor(valueLayout, renderer.MakeMetricTextBinding(widget, metric_, 100));
    }
    const RenderRect ringBounds{layoutState_.cx - layoutState_.outerRadius,
        layoutState_.cy - layoutState_.outerRadius,
        layoutState_.cx + layoutState_.outerRadius,
        layoutState_.cy + layoutState_.outerRadius};
    renderer.RegisterDynamicColorEditRegion(DashboardRenderer::LayoutEditParameter::ColorAccent,
        RenderRect{ringBounds.left, ringBounds.top, layoutState_.cx, ringBounds.bottom});
    renderer.RegisterDynamicColorEditRegion(DashboardRenderer::LayoutEditParameter::ColorTrack,
        RenderRect{layoutState_.cx, ringBounds.top, ringBounds.right, ringBounds.bottom});
    renderer.DrawText(layoutState_.labelRect,
        metric.label,
        TextStyleId::Small,
        RenderColorId::MutedText,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
}

void GaugeWidget::BuildStaticAnchors(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int cx = layoutState_.cx;
    const int cy = layoutState_.cy;
    const int outerRadius = layoutState_.outerRadius;
    renderer.RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::GaugeSegmentCount,
            0},
        widget.rect,
        layoutState_.segmentCountAnchorRect,
        AnchorShape::Diamond,
        AnchorDragAxis::Both,
        AnchorDragMode::AxisDelta,
        RenderPoint{cx, cy - outerRadius},
        1.0,
        true,
        true,
        true,
        renderer.Config().layout.gauge.segmentCount);
    renderer.RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::GaugeOuterPadding,
            0},
        layoutState_.outerPaddingAnchorRect,
        layoutState_.outerPaddingAnchorRect,
        AnchorShape::Circle,
        AnchorDragAxis::Both,
        AnchorDragMode::RadialDistance,
        RenderPoint{cx, cy},
        -1.0,
        true,
        true,
        false,
        renderer.Config().layout.gauge.outerPadding);
    renderer.RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::GaugeRingThickness,
            0},
        layoutState_.ringThicknessAnchorRect,
        layoutState_.ringThicknessAnchorRect,
        AnchorShape::Circle,
        AnchorDragAxis::Both,
        AnchorDragMode::RadialDistance,
        RenderPoint{cx, cy},
        -1.0,
        true,
        true,
        false,
        renderer.Config().layout.gauge.ringThickness);
    const MetricDefinitionConfig* definition = renderer.FindConfiguredMetricDefinition(metric_);
    if (definition != nullptr && !definition->label.empty()) {
        renderer.RegisterStaticTextAnchor(layoutState_.labelRect,
            definition->label,
            TextStyleId::Small,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center),
            renderer.MakeEditableTextBinding(widget,
                DashboardRenderer::LayoutEditParameter::FontSmall,
                1,
                renderer.Config().layout.fonts.smallText.size),
            DashboardRenderer::LayoutEditParameter::ColorMutedText);
        renderer.RegisterStaticTextAnchor(layoutState_.labelRect,
            definition->label,
            TextStyleId::Small,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center),
            renderer.MakeMetricTextBinding(widget, metric_, 101));
    }
}

void GaugeWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
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

    const auto addRadialGuide = [&](DashboardRenderer::LayoutEditParameter parameter,
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
        renderer.WidgetEditGuidesMutable().push_back(std::move(guide));
    };

    addRadialGuide(DashboardRenderer::LayoutEditParameter::GaugeSweepDegrees,
        0,
        gaugeLayout.gaugeEnd,
        gaugeLayout.totalSweep,
        0.0,
        360.0);
    addRadialGuide(DashboardRenderer::LayoutEditParameter::GaugeSegmentGapDegrees,
        gaugeLayout.segmentCount,
        gaugeLayout.gaugeStart + gaugeLayout.segmentSweep,
        gaugeLayout.segmentGap,
        gaugeLayout.gaugeStart,
        gaugeLayout.gaugeStart + gaugeLayout.maxSegmentSweep);

    const auto addHorizontalGuide =
        [&](DashboardRenderer::LayoutEditParameter parameter, int guideId, int bottomOffset) {
            const int y = cy + renderer.ScaleLogical(bottomOffset);
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
            renderer.WidgetEditGuidesMutable().push_back(std::move(guide));
        };

    addHorizontalGuide(
        DashboardRenderer::LayoutEditParameter::GaugeValueBottom, 100, renderer.Config().layout.gauge.valueBottom);
    addHorizontalGuide(
        DashboardRenderer::LayoutEditParameter::GaugeLabelBottom, 101, renderer.Config().layout.gauge.labelBottom);
}

void GaugeWidget::FinalizeLayoutGroup(DashboardRenderer& renderer, const std::vector<DashboardWidgetLayout*>& widgets) {
    auto sharedLayout = std::make_shared<GaugeSharedLayout>();
    int gaugeCount = 0;
    for (DashboardWidgetLayout* widget : widgets) {
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

    for (DashboardWidgetLayout* widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        GaugeWidget* gauge = dynamic_cast<GaugeWidget*>(widget->widget.get());
        if (gauge != nullptr) {
            gauge->sharedLayout_ = sharedLayout;
        }
    }
}
