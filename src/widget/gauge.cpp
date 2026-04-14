#include "gauge.h"

#include <algorithm>
#include <cmath>

#include <objidl.h>

#include <gdiplus.h>

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

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

Gdiplus::PointF GaugePoint(float cx, float cy, float radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return Gdiplus::PointF(cx + static_cast<Gdiplus::REAL>(std::cos(radians) * radius),
        cy + static_cast<Gdiplus::REAL>(std::sin(radians) * radius));
}

std::shared_ptr<Gdiplus::GraphicsPath> BuildCombinedGaugePath(
    const std::vector<std::shared_ptr<Gdiplus::GraphicsPath>>& segmentPaths, size_t segmentCount) {
    auto combinedPath = std::make_shared<Gdiplus::GraphicsPath>();
    for (size_t i = 0; i < segmentCount && i < segmentPaths.size(); ++i) {
        if (segmentPaths[i] != nullptr) {
            combinedPath->AddPath(segmentPaths[i].get(), FALSE);
        }
    }
    return combinedPath;
}

std::shared_ptr<Gdiplus::GraphicsPath> BuildGaugeSegmentPath(
    float cx, float cy, float outerRadius, float thickness, double startAngleDegrees, double sweepAngleDegrees) {
    if (outerRadius <= 0.0f || thickness <= 0.0f || sweepAngleDegrees <= 0.0) {
        return {};
    }

    const float innerRadius = (std::max)(0.0f, outerRadius - thickness);
    if (outerRadius <= innerRadius) {
        return {};
    }

    const float outerDiameter = outerRadius * 2.0f;
    const float innerDiameter = innerRadius * 2.0f;
    const Gdiplus::RectF outerRect(cx - outerRadius, cy - outerRadius, outerDiameter, outerDiameter);
    const Gdiplus::RectF innerRect(cx - innerRadius, cy - innerRadius, innerDiameter, innerDiameter);
    const Gdiplus::PointF outerStart = GaugePoint(cx, cy, outerRadius, startAngleDegrees);
    const Gdiplus::PointF outerEnd = GaugePoint(cx, cy, outerRadius, startAngleDegrees + sweepAngleDegrees);
    const Gdiplus::PointF innerEnd = GaugePoint(cx, cy, innerRadius, startAngleDegrees + sweepAngleDegrees);
    const Gdiplus::PointF innerStart = GaugePoint(cx, cy, innerRadius, startAngleDegrees);

    auto path = std::make_shared<Gdiplus::GraphicsPath>();
    path->StartFigure();
    path->AddArc(
        outerRect, static_cast<Gdiplus::REAL>(startAngleDegrees), static_cast<Gdiplus::REAL>(sweepAngleDegrees));
    path->AddLine(outerEnd, innerEnd);
    if (innerRadius > 0.0f) {
        path->AddArc(innerRect,
            static_cast<Gdiplus::REAL>(startAngleDegrees + sweepAngleDegrees),
            static_cast<Gdiplus::REAL>(-sweepAngleDegrees));
    } else {
        path->AddLine(innerEnd, Gdiplus::PointF(cx, cy));
    }
    path->AddLine(innerStart, outerStart);
    path->CloseFigure();
    return path;
}

POINT PolarPoint(int cx, int cy, int radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return POINT{cx + static_cast<LONG>(std::lround(std::cos(radians) * static_cast<double>(radius))),
        cy + static_cast<LONG>(std::lround(std::sin(radians) * static_cast<double>(radius)))};
}

RECT ExpandSegmentBounds(POINT start, POINT end, int inset) {
    return RECT{((std::min))(start.x, end.x) - inset,
        ((std::min))(start.y, end.y) - inset,
        ((std::max))(start.x, end.x) + inset + 1,
        ((std::max))(start.y, end.y) + inset + 1};
}

RECT MakeCircleAnchorRect(int centerX, int centerY, int representedDiameter, int extraDiameter) {
    const int diameter = std::max(4, representedDiameter + extraDiameter);
    const int radius = diameter / 2;
    return RECT{centerX - radius, centerY - radius, centerX - radius + diameter, centerY - radius + diameter};
}

int GaugeOuterRadiusForRect(const DashboardRenderer& renderer, const RECT& rect) {
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    const int outerPadding = std::max(0, renderer.ScaleLogical(renderer.Config().layout.gauge.outerPadding));
    return std::max(1, std::min(width, height) / 2 - outerPadding);
}

int GaugeTextHalfWidth(const DashboardRenderer& renderer) {
    const int valueWidth = renderer.MeasureTextWidth(renderer.WidgetFonts().big, "100%");
    const int labelWidth = renderer.MeasureTextWidth(renderer.WidgetFonts().smallFont, "Load");
    return std::max(1, ((std::max)(valueWidth, labelWidth) + 1) / 2);
}

int EffectiveGaugePreferredRadius(const DashboardRenderer& renderer) {
    const int outerPadding = std::max(0, renderer.ScaleLogical(renderer.Config().layout.gauge.outerPadding));
    const int ringThickness = std::max(1, renderer.ScaleLogical(renderer.Config().layout.gauge.ringThickness));
    const int halfWidth = GaugeTextHalfWidth(renderer);
    const int valueHalfHeight = std::max(1, (renderer.FontMetrics().big + 1) / 2);
    const int labelHalfHeight = std::max(1, (renderer.FontMetrics().smallText + 1) / 2);
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
    return EffectiveGaugePreferredRadius(renderer) * 2;
}

void GaugeWidget::ResolveLayoutState(const DashboardRenderer& renderer, const RECT& rect) {
    layoutState_ = {};
    layoutState_.outerRadius = sharedLayout_ != nullptr && sharedLayout_->radius > 0
                                   ? sharedLayout_->radius
                                   : GaugeOuterRadiusForRect(renderer, rect);
    layoutState_.cx = rect.left + ((std::max)(0L, rect.right - rect.left) / 2);
    layoutState_.cy = rect.top + ((std::max)(0L, rect.bottom - rect.top) / 2);
    layoutState_.ringThickness = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.gauge.ringThickness));
    layoutState_.segmentLayout = ComputeGaugeSegmentLayout(renderer.Config().layout.gauge.sweepDegrees,
        renderer.Config().layout.gauge.segmentCount,
        renderer.Config().layout.gauge.segmentGapDegrees);
    layoutState_.innerRadius = std::max(0, layoutState_.outerRadius - layoutState_.ringThickness);
    layoutState_.anchorPadding = std::max(1, renderer.ScaleLogical(1));
    layoutState_.anchorSize = (std::max)(4, renderer.ScaleLogical(6));
    layoutState_.anchorHalf = layoutState_.anchorSize / 2;
    layoutState_.halfWidth = GaugeTextHalfWidth(renderer);
    layoutState_.valueBottom = renderer.ScaleLogical(renderer.Config().layout.gauge.valueBottom);
    layoutState_.valueHeight = renderer.FontMetrics().big;
    layoutState_.labelBottom = renderer.ScaleLogical(renderer.Config().layout.gauge.labelBottom);
    layoutState_.labelHeight = renderer.FontMetrics().smallText;
    layoutState_.guideHalfExtension = (std::max)(1, layoutState_.ringThickness / 2);
    layoutState_.hitInset = (std::max)(4, renderer.ScaleLogical(5));
    layoutState_.segmentCountAnchorRect = RECT{layoutState_.cx - layoutState_.anchorHalf,
        layoutState_.cy - layoutState_.outerRadius - layoutState_.anchorHalf,
        layoutState_.cx - layoutState_.anchorHalf + layoutState_.anchorSize,
        layoutState_.cy - layoutState_.outerRadius - layoutState_.anchorHalf + layoutState_.anchorSize};
    layoutState_.outerPaddingAnchorRect = MakeCircleAnchorRect(
        layoutState_.cx, layoutState_.cy, layoutState_.outerRadius * 2, layoutState_.anchorPadding);
    layoutState_.ringThicknessAnchorRect = MakeCircleAnchorRect(
        layoutState_.cx, layoutState_.cy, layoutState_.innerRadius * 2, layoutState_.anchorPadding);
    layoutState_.valueRect = RECT{layoutState_.cx - layoutState_.halfWidth,
        layoutState_.cy + layoutState_.valueBottom - layoutState_.valueHeight,
        layoutState_.cx + layoutState_.halfWidth,
        layoutState_.cy + layoutState_.valueBottom};
    layoutState_.labelRect = RECT{layoutState_.cx - layoutState_.halfWidth,
        layoutState_.cy + layoutState_.labelBottom - layoutState_.labelHeight,
        layoutState_.cx + layoutState_.halfWidth,
        layoutState_.cy + layoutState_.labelBottom};
    layoutState_.segmentPaths.clear();
    layoutState_.segmentPaths.reserve(static_cast<size_t>(layoutState_.segmentLayout.segmentCount));
    for (int i = 0; i < layoutState_.segmentLayout.segmentCount; ++i) {
        const double slotStart =
            layoutState_.segmentLayout.gaugeStart + layoutState_.segmentLayout.pitchSweep * static_cast<double>(i);
        layoutState_.segmentPaths.push_back(BuildGaugeSegmentPath(static_cast<float>(layoutState_.cx),
            static_cast<float>(layoutState_.cy),
            static_cast<float>(layoutState_.outerRadius),
            static_cast<float>(layoutState_.ringThickness),
            slotStart,
            layoutState_.segmentLayout.segmentSweep));
    }
    layoutState_.trackPath = BuildCombinedGaugePath(layoutState_.segmentPaths, layoutState_.segmentPaths.size());
    layoutState_.usagePaths.clear();
    layoutState_.usagePaths.reserve(static_cast<size_t>(layoutState_.segmentLayout.segmentCount));
    for (size_t i = 1; i <= layoutState_.segmentPaths.size(); ++i) {
        layoutState_.usagePaths.push_back(BuildCombinedGaugePath(layoutState_.segmentPaths, i));
    }
}

void GaugeWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    const DashboardGaugeMetric metric = metrics.ResolveGauge(metric_);
    const GaugeSegmentLayout& gaugeLayout = layoutState_.segmentLayout;
    const double clampedPercent = std::clamp(metric.percent, 0.0, 100.0);
    const int filledSegments =
        clampedPercent <= 0.0
            ? 0
            : std::clamp(
                  static_cast<int>(std::ceil(clampedPercent * static_cast<double>(gaugeLayout.segmentCount) / 100.0)),
                  1,
                  gaugeLayout.segmentCount);
    const double clampedPeakRatio = std::clamp(metric.peakRatio, 0.0, 1.0);
    const int peakSegment =
        clampedPeakRatio <= 0.0
            ? -1
            : std::clamp(
                  static_cast<int>(std::ceil(clampedPeakRatio * static_cast<double>(gaugeLayout.segmentCount))) - 1,
                  0,
                  gaugeLayout.segmentCount - 1);

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const Gdiplus::Color trackColor(
        255, GetRValue(renderer.TrackColor()), GetGValue(renderer.TrackColor()), GetBValue(renderer.TrackColor()));
    const Gdiplus::Color usageColor(
        255, GetRValue(renderer.AccentColor()), GetGValue(renderer.AccentColor()), GetBValue(renderer.AccentColor()));
    const Gdiplus::Color ghostColor(
        96, GetRValue(renderer.AccentColor()), GetGValue(renderer.AccentColor()), GetBValue(renderer.AccentColor()));
    Gdiplus::SolidBrush trackBrush(trackColor);
    Gdiplus::SolidBrush usageBrush(usageColor);
    Gdiplus::SolidBrush ghostBrush(ghostColor);
    if (layoutState_.trackPath != nullptr) {
        graphics.FillPath(&trackBrush, layoutState_.trackPath.get());
    }
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank && filledSegments > 0 &&
        static_cast<size_t>(filledSegments - 1) < layoutState_.usagePaths.size() &&
        layoutState_.usagePaths[static_cast<size_t>(filledSegments - 1)] != nullptr) {
        graphics.FillPath(&usageBrush, layoutState_.usagePaths[static_cast<size_t>(filledSegments - 1)].get());
    }
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank && peakSegment >= 0 &&
        static_cast<size_t>(peakSegment) < layoutState_.segmentPaths.size() &&
        layoutState_.segmentPaths[static_cast<size_t>(peakSegment)] != nullptr) {
        graphics.FillPath(&ghostBrush, layoutState_.segmentPaths[static_cast<size_t>(peakSegment)].get());
    }

    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
        char number[16];
        sprintf_s(number, "%.0f%%", metric.percent);
        renderer.DrawText(hdc,
            layoutState_.valueRect,
            number,
            renderer.WidgetFonts().big,
            renderer.ForegroundColor(),
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        renderer.RegisterDynamicTextAnchor(layoutState_.valueRect,
            number,
            renderer.WidgetFonts().big,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER,
            renderer.MakeEditableTextBinding(
                widget, DashboardRenderer::LayoutEditParameter::FontBig, 0, renderer.Config().layout.fonts.big.size));
    }
    renderer.DrawText(hdc,
        layoutState_.labelRect,
        "Load",
        renderer.WidgetFonts().smallFont,
        renderer.MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void GaugeWidget::BuildStaticAnchors(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int cx = layoutState_.cx;
    const int cy = layoutState_.cy;
    const int outerRadius = layoutState_.outerRadius;
    renderer.RegisterStaticEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::GaugeSegmentCount,
            0,
        },
        widget.rect,
        layoutState_.segmentCountAnchorRect,
        DashboardRenderer::AnchorShape::Diamond,
        DashboardRenderer::AnchorDragAxis::Both,
        DashboardRenderer::AnchorDragMode::AxisDelta,
        POINT{cx, cy - outerRadius},
        1.0,
        true,
        true,
        renderer.Config().layout.gauge.segmentCount);
    renderer.RegisterStaticEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::GaugeOuterPadding,
            0,
        },
        layoutState_.outerPaddingAnchorRect,
        layoutState_.outerPaddingAnchorRect,
        DashboardRenderer::AnchorShape::Circle,
        DashboardRenderer::AnchorDragAxis::Both,
        DashboardRenderer::AnchorDragMode::RadialDistance,
        POINT{cx, cy},
        -1.0,
        true,
        false,
        renderer.Config().layout.gauge.outerPadding);
    renderer.RegisterStaticEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::GaugeRingThickness,
            0,
        },
        layoutState_.ringThicknessAnchorRect,
        layoutState_.ringThicknessAnchorRect,
        DashboardRenderer::AnchorShape::Circle,
        DashboardRenderer::AnchorDragAxis::Both,
        DashboardRenderer::AnchorDragMode::RadialDistance,
        POINT{cx, cy},
        -1.0,
        true,
        false,
        renderer.Config().layout.gauge.ringThickness);
    renderer.RegisterStaticTextAnchor(layoutState_.labelRect,
        "Load",
        renderer.WidgetFonts().smallFont,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::LayoutEditParameter::FontSmall,
            1,
            renderer.Config().layout.fonts.smallText.size));
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
        const POINT guideStart = PolarPoint(cx, cy, innerGuideRadius, angleDegrees);
        const POINT guideEnd = PolarPoint(cx, cy, outerGuideRadius, angleDegrees);
        DashboardRenderer::WidgetEditGuide guide;
        guide.axis = DashboardRenderer::LayoutGuideAxis::Vertical;
        guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.drawStart = guideStart;
        guide.drawEnd = guideEnd;
        guide.hitRect = ExpandSegmentBounds(guideStart, guideEnd, hitInset);
        guide.dragOrigin = POINT{cx, cy};
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
            DashboardRenderer::WidgetEditGuide guide;
            guide.axis = DashboardRenderer::LayoutGuideAxis::Horizontal;
            guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
            guide.parameter = parameter;
            guide.guideId = guideId;
            guide.widgetRect = widget.rect;
            guide.drawStart = POINT{cx - halfWidth, y};
            guide.drawEnd = POINT{cx + halfWidth, y};
            guide.hitRect = RECT{cx - halfWidth, y - hitInset, cx + halfWidth, y + hitInset + 1};
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
