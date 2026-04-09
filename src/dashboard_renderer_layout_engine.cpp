#include "dashboard_renderer_layout_engine.h"

#include "dashboard_renderer.h"

#include <algorithm>
#include <cmath>

namespace {

POINT PolarPoint(int cx, int cy, int radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return POINT{
        cx + static_cast<LONG>(std::lround(std::cos(radians) * static_cast<double>(radius))),
        cy + static_cast<LONG>(std::lround(std::sin(radians) * static_cast<double>(radius)))
    };
}

RECT ExpandSegmentBounds(POINT start, POINT end, int inset) {
    return RECT{
        (std::min)(start.x, end.x) - inset,
        (std::min)(start.y, end.y) - inset,
        (std::max)(start.x, end.x) + inset + 1,
        (std::max)(start.y, end.y) + inset + 1
    };
}

struct GaugeSegmentLayout {
    int segmentCount = 1;
    double totalSweep = 0.0;
    double segmentGap = 0.0;
    double segmentSweep = 0.0;
    double maxSegmentSweep = 0.0;
    double gaugeStart = 90.0;
    double gaugeEnd = 90.0;
};

GaugeSegmentLayout ComputeGaugeSegmentLayout(double requestedSweep, int requestedSegmentCount, double requestedSegmentGap) {
    GaugeSegmentLayout layout;
    layout.segmentCount = (std::max)(1, requestedSegmentCount);
    layout.totalSweep = std::clamp(requestedSweep, 0.0, 360.0);
    const double gapSweep = (std::max)(0.0, 360.0 - layout.totalSweep);
    layout.gaugeStart = 90.0 + (gapSweep / 2.0);
    layout.gaugeEnd = layout.gaugeStart + layout.totalSweep;

    if (layout.segmentCount <= 1) {
        layout.segmentGap = 0.0;
        layout.segmentSweep = layout.totalSweep;
        layout.maxSegmentSweep = layout.totalSweep;
        return layout;
    }

    layout.maxSegmentSweep = layout.totalSweep / static_cast<double>(layout.segmentCount);
    const double maxSegmentGap = layout.totalSweep / static_cast<double>(layout.segmentCount - 1);
    layout.segmentGap = std::clamp(requestedSegmentGap, 0.0, maxSegmentGap);
    layout.segmentSweep = (std::max)(0.0,
        (layout.totalSweep - (layout.segmentGap * static_cast<double>(layout.segmentCount - 1))) /
            static_cast<double>(layout.segmentCount));
    return layout;
}

}  // namespace

void DashboardRendererLayoutEngine::BuildWidgetEditGuides(DashboardRenderer& renderer) {
    const auto addMetricListWidgetEditGuides = [&](const DashboardRenderer::ResolvedWidgetLayout& widget) {
        const int labelWidth = (std::max)(1, renderer.ScaleLogical(renderer.config_.layout.metricList.labelWidth));
        const int rowHeight = renderer.EffectiveMetricRowHeight();
        const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
        const int x = std::clamp(static_cast<int>(widget.rect.left) + labelWidth,
            static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
        const int totalRows = widget.binding.param.empty()
            ? 0
            : 1 + static_cast<int>(std::count(widget.binding.param.begin(), widget.binding.param.end(), ','));
        const int visibleRows = rowHeight > 0
            ? std::clamp(((std::max)(0, static_cast<int>(widget.rect.bottom - widget.rect.top)) + rowHeight - 1) / rowHeight, 0, totalRows)
            : 0;

        const auto addGuide = [&](DashboardRenderer::LayoutGuideAxis axis, int guideId, DashboardRenderer::WidgetEditParameter parameter,
            const RECT& lineRect, const RECT& hitRect, int value, int dragDirection) {
            DashboardRenderer::WidgetEditGuide guide;
            guide.axis = axis;
            guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
            guide.parameter = parameter;
            guide.guideId = guideId;
            guide.widgetRect = widget.rect;
            guide.drawStart = POINT{lineRect.left, lineRect.top};
            guide.drawEnd = axis == DashboardRenderer::LayoutGuideAxis::Vertical
                ? POINT{lineRect.left, lineRect.bottom}
                : POINT{lineRect.right, lineRect.top};
            guide.hitRect = hitRect;
            guide.value = value;
            guide.dragDirection = dragDirection;
            renderer.widgetEditGuides_.push_back(std::move(guide));
        };

        addGuide(DashboardRenderer::LayoutGuideAxis::Vertical, 0, DashboardRenderer::WidgetEditParameter::MetricListLabelWidth,
            RECT{x, widget.rect.top, x + 1, widget.rect.bottom},
            RECT{x - hitInset, widget.rect.top, x + hitInset + 1, widget.rect.bottom},
            renderer.config_.layout.metricList.labelWidth, 1);

        for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
            const int y = widget.rect.top + ((rowIndex + 1) * rowHeight);
            addGuide(DashboardRenderer::LayoutGuideAxis::Horizontal, 1 + rowIndex, DashboardRenderer::WidgetEditParameter::MetricListVerticalGap,
                RECT{widget.rect.left, y, widget.rect.right, y + 1},
                RECT{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1},
                renderer.config_.layout.metricList.verticalGap, 1);
        }
    };

    const auto addDriveUsageWidgetEditGuides = [&](const DashboardRenderer::ResolvedWidgetLayout& widget) {
        const int headerHeight = renderer.EffectiveDriveHeaderHeight();
        const int rowHeight = renderer.EffectiveDriveRowHeight();
        const int labelWidth = (std::max)(1, renderer.measuredWidths_.driveLabel);
        const int percentWidth = (std::max)(1, renderer.measuredWidths_.drivePercent);
        const int freeWidth = (std::max)(1, renderer.ScaleLogical(renderer.config_.layout.driveUsageList.freeWidth));
        const int activityWidth = (std::max)(1, renderer.ScaleLogical(renderer.config_.layout.driveUsageList.activityWidth));
        const int barGap = (std::max)(0, renderer.ScaleLogical(renderer.config_.layout.driveUsageList.barGap));
        const int valueGap = (std::max)(0, renderer.ScaleLogical(renderer.config_.layout.driveUsageList.valueGap));
        const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
        const int totalRows = static_cast<int>(renderer.config_.storage.drives.size());
        const int availableRowPixels = (std::max)(0, static_cast<int>(widget.rect.bottom - widget.rect.top) - headerHeight);
        const int visibleRows = rowHeight > 0
            ? std::clamp((availableRowPixels + rowHeight - 1) / rowHeight, 0, totalRows)
            : 0;

        RECT labelRect{
            widget.rect.left,
            widget.rect.top,
            (std::min)(widget.rect.right, static_cast<LONG>(widget.rect.left + labelWidth)),
            widget.rect.bottom};
        RECT readRect{
            (std::min)(widget.rect.right, static_cast<LONG>(labelRect.right + barGap)),
            widget.rect.top,
            (std::min)(widget.rect.right, static_cast<LONG>(labelRect.right + barGap + activityWidth)),
            widget.rect.bottom};
        RECT writeRect{
            (std::min)(widget.rect.right, static_cast<LONG>(readRect.right + valueGap)),
            widget.rect.top,
            (std::min)(widget.rect.right, static_cast<LONG>(readRect.right + valueGap + activityWidth)),
            widget.rect.bottom};
        RECT freeRect{
            (std::max)(widget.rect.left, static_cast<LONG>(widget.rect.right - freeWidth)),
            widget.rect.top,
            widget.rect.right,
            widget.rect.bottom};
        RECT pctRect{
            (std::max)(widget.rect.left, static_cast<LONG>(freeRect.left - valueGap - percentWidth)),
            widget.rect.top,
            (std::max)(widget.rect.left, static_cast<LONG>(freeRect.left - valueGap)),
            widget.rect.bottom};
        if (pctRect.left < writeRect.right) {
            return;
        }

        const auto addVerticalGuide = [&](int guideId, int x, DashboardRenderer::WidgetEditParameter parameter, int value, int dragDirection) {
            const int clampedX = std::clamp(x, static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
            DashboardRenderer::WidgetEditGuide guide;
            guide.axis = DashboardRenderer::LayoutGuideAxis::Vertical;
            guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
            guide.parameter = parameter;
            guide.guideId = guideId;
            guide.widgetRect = widget.rect;
            guide.drawStart = POINT{clampedX, widget.rect.top};
            guide.drawEnd = POINT{clampedX, widget.rect.bottom};
            guide.hitRect = RECT{clampedX - hitInset, widget.rect.top, clampedX + hitInset + 1, widget.rect.bottom};
            guide.value = value;
            guide.dragDirection = dragDirection;
            renderer.widgetEditGuides_.push_back(std::move(guide));
        };
        const auto addHorizontalGuide = [&](int guideId, int y, DashboardRenderer::WidgetEditParameter parameter, int value, int dragDirection) {
            const int clampedY = std::clamp(y, static_cast<int>(widget.rect.top), static_cast<int>(widget.rect.bottom));
            DashboardRenderer::WidgetEditGuide guide;
            guide.axis = DashboardRenderer::LayoutGuideAxis::Horizontal;
            guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
            guide.parameter = parameter;
            guide.guideId = guideId;
            guide.widgetRect = widget.rect;
            guide.drawStart = POINT{widget.rect.left, clampedY};
            guide.drawEnd = POINT{widget.rect.right, clampedY};
            guide.hitRect = RECT{widget.rect.left, clampedY - hitInset, widget.rect.right, clampedY + hitInset + 1};
            guide.value = value;
            guide.dragDirection = dragDirection;
            renderer.widgetEditGuides_.push_back(std::move(guide));
        };

        addVerticalGuide(0, readRect.right, DashboardRenderer::WidgetEditParameter::DriveUsageActivityWidth,
            renderer.config_.layout.driveUsageList.activityWidth, 1);
        addVerticalGuide(1, writeRect.right, DashboardRenderer::WidgetEditParameter::DriveUsageActivityWidth,
            renderer.config_.layout.driveUsageList.activityWidth, 1);
        addVerticalGuide(2, freeRect.left, DashboardRenderer::WidgetEditParameter::DriveUsageFreeWidth,
            renderer.config_.layout.driveUsageList.freeWidth, -1);
        addHorizontalGuide(3, widget.rect.top + headerHeight, DashboardRenderer::WidgetEditParameter::DriveUsageHeaderGap,
            renderer.config_.layout.driveUsageList.headerGap, 1);
        for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
            const int y = widget.rect.top + headerHeight + ((rowIndex + 1) * rowHeight);
            addHorizontalGuide(4 + rowIndex, y, DashboardRenderer::WidgetEditParameter::DriveUsageRowGap,
                renderer.config_.layout.driveUsageList.rowGap, 1);
        }
    };

    const auto addThroughputWidgetEditGuide = [&](const DashboardRenderer::ResolvedWidgetLayout& widget) {
        const int lineHeight = renderer.fontHeights_.smallText + (std::max)(0, renderer.ScaleLogical(renderer.config_.layout.throughput.valuePadding));
        RECT valueRect{widget.rect.left, widget.rect.top, widget.rect.right, (std::min)(widget.rect.bottom, widget.rect.top + lineHeight)};
        RECT graphRect{widget.rect.left, (std::min)(widget.rect.bottom, valueRect.bottom + (std::max)(0, renderer.ScaleLogical(renderer.config_.layout.throughput.headerGap))),
            widget.rect.right, widget.rect.bottom};
        const int axisWidth = (std::max)(1, renderer.measuredWidths_.throughputAxis);
        const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
        const auto addGuide = [&](DashboardRenderer::LayoutGuideAxis axis, int guideId, DashboardRenderer::WidgetEditParameter parameter, const RECT& lineRect,
            const RECT& hitRect, int value, int dragDirection) {
            DashboardRenderer::WidgetEditGuide guide;
            guide.axis = axis;
            guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
            guide.parameter = parameter;
            guide.guideId = guideId;
            guide.widgetRect = widget.rect;
            guide.drawStart = POINT{lineRect.left, lineRect.top};
            guide.drawEnd = axis == DashboardRenderer::LayoutGuideAxis::Vertical
                ? POINT{lineRect.left, lineRect.bottom}
                : POINT{lineRect.right, lineRect.top};
            guide.hitRect = hitRect;
            guide.value = value;
            guide.dragDirection = dragDirection;
            renderer.widgetEditGuides_.push_back(std::move(guide));
        };

        const int x = std::clamp(static_cast<int>(graphRect.left) + axisWidth,
            static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
        addGuide(DashboardRenderer::LayoutGuideAxis::Vertical, 0, DashboardRenderer::WidgetEditParameter::ThroughputAxisPadding,
            RECT{x, graphRect.top, x + 1, graphRect.bottom},
            RECT{x - hitInset, graphRect.top, x + hitInset + 1, graphRect.bottom},
            renderer.config_.layout.throughput.axisPadding, 1);

        const int y = std::clamp(static_cast<int>(graphRect.top), static_cast<int>(widget.rect.top), static_cast<int>(widget.rect.bottom));
        addGuide(DashboardRenderer::LayoutGuideAxis::Horizontal, 1, DashboardRenderer::WidgetEditParameter::ThroughputHeaderGap,
            RECT{widget.rect.left, y, widget.rect.right, y + 1},
            RECT{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1},
            renderer.config_.layout.throughput.headerGap, 1);
    };

    const auto addGaugeWidgetEditGuide = [&](const DashboardRenderer::ResolvedWidgetLayout& widget) {
        const int radius = (std::min)((std::max)(1, renderer.resolvedLayout_.globalGaugeRadius), renderer.GaugeRadiusForRect(widget.rect));
        if (radius <= 0) {
            return;
        }

        const int cx = widget.rect.left + (std::max)(0L, widget.rect.right - widget.rect.left) / 2;
        const int cy = widget.rect.top + (std::max)(0L, widget.rect.bottom - widget.rect.top) / 2;
        const GaugeSegmentLayout gaugeLayout = ComputeGaugeSegmentLayout(
            renderer.config_.layout.gauge.sweepDegrees,
            renderer.config_.layout.gauge.segmentCount,
            renderer.config_.layout.gauge.segmentGapDegrees);
        const POINT center{cx, cy};
        const int ringThickness = (std::max)(1, renderer.ScaleLogical(renderer.config_.layout.gauge.ringThickness));
        const int guideHalfExtension = (std::max)(1, ringThickness / 2);
        const int hitInset = (std::max)(4, renderer.ScaleLogical(5));

        const auto addRadialGuide = [&](DashboardRenderer::WidgetEditParameter parameter, int guideId, double angleDegrees,
            double value, double angularMin, double angularMax) {
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
            guide.dragOrigin = center;
            guide.value = value;
            guide.angularDrag = true;
            guide.angularMin = angularMin;
            guide.angularMax = angularMax;
            renderer.widgetEditGuides_.push_back(std::move(guide));
        };

        addRadialGuide(DashboardRenderer::WidgetEditParameter::GaugeSweepDegrees, 0, gaugeLayout.gaugeEnd, gaugeLayout.totalSweep, 0.0, 360.0);
        addRadialGuide(DashboardRenderer::WidgetEditParameter::GaugeSegmentGapDegrees, gaugeLayout.segmentCount,
            gaugeLayout.gaugeStart + gaugeLayout.segmentSweep,
            gaugeLayout.segmentGap, gaugeLayout.gaugeStart, gaugeLayout.gaugeStart + gaugeLayout.maxSegmentSweep);
    };

    renderer.widgetEditGuides_.clear();
    for (const auto& card : renderer.resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.kind == DashboardRenderer::WidgetKind::Gauge) {
                addGaugeWidgetEditGuide(widget);
            }
            if (widget.kind == DashboardRenderer::WidgetKind::MetricList) {
                addMetricListWidgetEditGuides(widget);
            }
            if (widget.kind == DashboardRenderer::WidgetKind::Throughput) {
                addThroughputWidgetEditGuide(widget);
            }
            if (widget.kind == DashboardRenderer::WidgetKind::DriveUsageList) {
                addDriveUsageWidgetEditGuides(widget);
            }
        }
    }
}
