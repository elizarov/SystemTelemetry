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

struct GaugeSegmentLayout {
    int segmentCount = 1;
    double totalSweep = 0.0;
    double gapSweep = 360.0;
    double segmentGap = 0.0;
    double segmentSweep = 0.0;
    double pitchSweep = 0.0;
    double gaugeStart = 90.0;
    double gaugeEnd = 90.0;
    double maxSegmentSweep = 0.0;
};

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
    const double maxSegmentGap = layout.totalSweep / static_cast<double>(layout.segmentCount - 1);
    layout.segmentGap = std::clamp(requestedSegmentGap, 0.0, maxSegmentGap);
    layout.segmentSweep = (std::max)(0.0,
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

void FillGaugeSegment(Gdiplus::Graphics& graphics,
    float cx,
    float cy,
    float radius,
    float thickness,
    double startAngleDegrees,
    double sweepAngleDegrees,
    const Gdiplus::Color& color) {
    if (radius <= 0.0f || thickness <= 0.0f || sweepAngleDegrees <= 0.0) {
        return;
    }

    const float outerRadius = radius + (thickness / 2.0f);
    const float innerRadius = (std::max)(0.0f, radius - (thickness / 2.0f));
    if (outerRadius <= innerRadius) {
        return;
    }

    const float outerDiameter = outerRadius * 2.0f;
    const float innerDiameter = innerRadius * 2.0f;
    const Gdiplus::RectF outerRect(cx - outerRadius, cy - outerRadius, outerDiameter, outerDiameter);
    const Gdiplus::RectF innerRect(cx - innerRadius, cy - innerRadius, innerDiameter, innerDiameter);
    const Gdiplus::PointF outerStart = GaugePoint(cx, cy, outerRadius, startAngleDegrees);
    const Gdiplus::PointF outerEnd = GaugePoint(cx, cy, outerRadius, startAngleDegrees + sweepAngleDegrees);
    const Gdiplus::PointF innerEnd = GaugePoint(cx, cy, innerRadius, startAngleDegrees + sweepAngleDegrees);
    const Gdiplus::PointF innerStart = GaugePoint(cx, cy, innerRadius, startAngleDegrees);

    Gdiplus::GraphicsPath path;
    path.StartFigure();
    path.AddArc(
        outerRect, static_cast<Gdiplus::REAL>(startAngleDegrees), static_cast<Gdiplus::REAL>(sweepAngleDegrees));
    path.AddLine(outerEnd, innerEnd);
    if (innerRadius > 0.0f) {
        path.AddArc(innerRect,
            static_cast<Gdiplus::REAL>(startAngleDegrees + sweepAngleDegrees),
            static_cast<Gdiplus::REAL>(-sweepAngleDegrees));
    } else {
        path.AddLine(innerEnd, Gdiplus::PointF(cx, cy));
    }
    path.AddLine(innerStart, outerStart);
    path.CloseFigure();

    Gdiplus::SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
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

int GaugeRadiusForRect(const DashboardRenderer& renderer, const RECT& rect) {
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    const int outerPadding = std::max(0, renderer.ScaleLogical(renderer.Config().layout.gauge.outerPadding));
    return std::max(1, std::min(width, height) / 2 - outerPadding);
}

int EffectiveGaugePreferredRadius(const DashboardRenderer& renderer) {
    const int outerPadding = std::max(0, renderer.ScaleLogical(renderer.Config().layout.gauge.outerPadding));
    const int ringThickness = std::max(1, renderer.ScaleLogical(renderer.Config().layout.gauge.ringThickness));
    const int halfWidth = std::max(1, renderer.ScaleLogical(renderer.Config().layout.gauge.textHalfWidth));
    const int valueTop = std::max(0, renderer.ScaleLogical(renderer.Config().layout.gauge.valueTop));
    const int valueBottom = std::max(0, renderer.ScaleLogical(renderer.Config().layout.gauge.valueBottom));
    const int labelTop = std::max(0, renderer.ScaleLogical(renderer.Config().layout.gauge.labelTop));
    const int labelBottom = std::max(0, renderer.ScaleLogical(renderer.Config().layout.gauge.labelBottom));
    const int valueHalfHeight = (std::max)(1, (renderer.FontMetrics().big + 1) / 2);
    const int labelHalfHeight = (std::max)(1, (renderer.FontMetrics().smallText + 1) / 2);
    const int verticalInnerExtent = (std::max)(valueTop + valueHalfHeight,
        (std::max)(
            valueBottom + valueHalfHeight, (std::max)(labelTop + labelHalfHeight, labelBottom + labelHalfHeight)));
    const int innerRadius = (std::max)(halfWidth, verticalInnerExtent);
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

void GaugeWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    const DashboardGaugeMetric metric = metrics.ResolveGauge(metric_);
    const int width = widget.rect.right - widget.rect.left;
    const int height = widget.rect.bottom - widget.rect.top;
    const int radius = sharedLayout_ != nullptr && sharedLayout_->radius > 0
                           ? sharedLayout_->radius
                           : GaugeRadiusForRect(renderer, widget.rect);
    const int cx = widget.rect.left + width / 2;
    const int cy = widget.rect.top + height / 2;
    const float segmentThickness =
        static_cast<float>((std::max)(1, renderer.ScaleLogical(renderer.Config().layout.gauge.ringThickness)));
    const GaugeSegmentLayout gaugeLayout = ComputeGaugeSegmentLayout(renderer.Config().layout.gauge.sweepDegrees,
        renderer.Config().layout.gauge.segmentCount,
        renderer.Config().layout.gauge.segmentGapDegrees);
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
    const int anchorPadding = std::max(1, renderer.ScaleLogical(1));
    const int anchorSize = (std::max)(4, renderer.ScaleLogical(6));
    const int anchorHalf = anchorSize / 2;
    const int outerRadius = radius + static_cast<int>(std::ceil(static_cast<double>(segmentThickness) / 2.0f));
    const int innerRadius =
        std::max(0, radius - static_cast<int>(std::floor(static_cast<double>(segmentThickness) / 2.0f)));
    renderer.RegisterEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::AnchorEditParameter::SegmentCount,
            0,
        },
        widget.rect,
        RECT{cx - anchorHalf,
            cy - outerRadius - anchorHalf,
            cx - anchorHalf + anchorSize,
            cy - outerRadius - anchorHalf + anchorSize},
        DashboardRenderer::AnchorShape::Diamond,
        DashboardRenderer::AnchorDragAxis::Both,
        DashboardRenderer::AnchorDragMode::AxisDelta,
        POINT{cx, cy - outerRadius},
        1.0,
        true,
        true,
        renderer.Config().layout.gauge.segmentCount);
    const RECT outerPaddingAnchorRect =
        MakeCircleAnchorRect(cx, cy, outerRadius * 2, anchorPadding);
    renderer.RegisterEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::AnchorEditParameter::GaugeOuterPadding,
            0,
        },
        outerPaddingAnchorRect,
        outerPaddingAnchorRect,
        DashboardRenderer::AnchorShape::Circle,
        DashboardRenderer::AnchorDragAxis::Both,
        DashboardRenderer::AnchorDragMode::RadialDistance,
        POINT{cx, cy},
        -1.0,
        true,
        false,
        renderer.Config().layout.gauge.outerPadding);
    const RECT ringThicknessAnchorRect =
        MakeCircleAnchorRect(cx, cy, innerRadius * 2, anchorPadding);
    renderer.RegisterEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::AnchorEditParameter::GaugeRingThickness,
            0,
        },
        ringThicknessAnchorRect,
        ringThicknessAnchorRect,
        DashboardRenderer::AnchorShape::Circle,
        DashboardRenderer::AnchorDragAxis::Both,
        DashboardRenderer::AnchorDragMode::RadialDistance,
        POINT{cx, cy},
        -2.0,
        true,
        false,
        renderer.Config().layout.gauge.ringThickness);

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const Gdiplus::Color trackColor(
        255, GetRValue(renderer.TrackColor()), GetGValue(renderer.TrackColor()), GetBValue(renderer.TrackColor()));
    const Gdiplus::Color usageColor(
        255, GetRValue(renderer.AccentColor()), GetGValue(renderer.AccentColor()), GetBValue(renderer.AccentColor()));
    const Gdiplus::Color ghostColor(
        96, GetRValue(renderer.AccentColor()), GetGValue(renderer.AccentColor()), GetBValue(renderer.AccentColor()));

    for (int i = 0; i < gaugeLayout.segmentCount; ++i) {
        const double slotStart = gaugeLayout.gaugeStart + gaugeLayout.pitchSweep * static_cast<double>(i);
        FillGaugeSegment(graphics,
            static_cast<float>(cx),
            static_cast<float>(cy),
            static_cast<float>(radius),
            segmentThickness,
            slotStart,
            gaugeLayout.segmentSweep,
            trackColor);

        if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank && i < filledSegments) {
            FillGaugeSegment(graphics,
                static_cast<float>(cx),
                static_cast<float>(cy),
                static_cast<float>(radius),
                segmentThickness,
                slotStart,
                gaugeLayout.segmentSweep,
                usageColor);
        }

        if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank && i == peakSegment) {
            FillGaugeSegment(graphics,
                static_cast<float>(cx),
                static_cast<float>(cy),
                static_cast<float>(radius),
                segmentThickness,
                slotStart,
                gaugeLayout.segmentSweep,
                ghostColor);
        }
    }

    const int halfWidth = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.gauge.textHalfWidth));
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
        char number[16];
        sprintf_s(number, "%.0f%%", metric.percent);
        renderer.DrawTextBlock(hdc,
            RECT{cx - halfWidth,
                cy - renderer.ScaleLogical(renderer.Config().layout.gauge.valueTop),
                cx + halfWidth,
                cy + renderer.ScaleLogical(renderer.Config().layout.gauge.valueBottom)},
            number,
            renderer.WidgetFonts().big,
            renderer.ForegroundColor(),
            DT_CENTER | DT_SINGLELINE | DT_VCENTER,
            renderer.MakeEditableTextBinding(
                widget, DashboardRenderer::AnchorEditParameter::FontBig, 0, renderer.Config().layout.fonts.big.size));
    }
    renderer.DrawTextBlock(hdc,
        RECT{cx - halfWidth,
            cy + renderer.ScaleLogical(renderer.Config().layout.gauge.labelTop),
            cx + halfWidth,
            cy + renderer.ScaleLogical(renderer.Config().layout.gauge.labelBottom)},
        "Load",
        renderer.WidgetFonts().smallFont,
        renderer.MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::AnchorEditParameter::FontSmall,
            1,
            renderer.Config().layout.fonts.smallText.size));
}

void GaugeWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int radius = sharedLayout_ != nullptr && sharedLayout_->radius > 0
                           ? sharedLayout_->radius
                           : GaugeRadiusForRect(renderer, widget.rect);
    if (radius <= 0) {
        return;
    }

    const int cx = widget.rect.left + ((std::max)(0L, widget.rect.right - widget.rect.left) / 2);
    const int cy = widget.rect.top + ((std::max)(0L, widget.rect.bottom - widget.rect.top) / 2);
    const GaugeSegmentLayout gaugeLayout = ComputeGaugeSegmentLayout(renderer.Config().layout.gauge.sweepDegrees,
        renderer.Config().layout.gauge.segmentCount,
        renderer.Config().layout.gauge.segmentGapDegrees);
    const int ringThickness = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.gauge.ringThickness));
    const int guideHalfExtension = (std::max)(1, ringThickness / 2);
    const int hitInset = (std::max)(4, renderer.ScaleLogical(5));

    const auto addRadialGuide = [&](DashboardRenderer::WidgetEditParameter parameter,
                                    int guideId,
                                    double angleDegrees,
                                    double value,
                                    double angularMin,
                                    double angularMax) {
        const int innerGuideRadius = (std::max)(1, radius - (ringThickness / 2) - guideHalfExtension);
        const int outerGuideRadius = radius + (ringThickness / 2) + guideHalfExtension;
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

    addRadialGuide(DashboardRenderer::WidgetEditParameter::GaugeSweepDegrees,
        0,
        gaugeLayout.gaugeEnd,
        gaugeLayout.totalSweep,
        0.0,
        360.0);
    addRadialGuide(DashboardRenderer::WidgetEditParameter::GaugeSegmentGapDegrees,
        gaugeLayout.segmentCount,
        gaugeLayout.gaugeStart + gaugeLayout.segmentSweep,
        gaugeLayout.segmentGap,
        gaugeLayout.gaugeStart,
        gaugeLayout.gaugeStart + gaugeLayout.maxSegmentSweep);
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
        const int gaugeRadius = GaugeRadiusForRect(renderer, widget->rect);
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
