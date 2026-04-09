#include "dashboard_renderer_layout_engine.h"

#include "dashboard_renderer.h"

#include <algorithm>
#include <cmath>
#include <functional>

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

std::string FormatRect(const RECT& rect) {
    return "rect=(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," +
        std::to_string(rect.right) + "," + std::to_string(rect.bottom) + ")";
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

bool DashboardRendererLayoutEngine::ResolveLayout(DashboardRenderer& renderer) {
    renderer.resolvedLayout_ = {};
    renderer.layoutEditGuides_.clear();
    renderer.widgetEditGuides_.clear();
    renderer.resolvedLayout_.windowWidth = renderer.WindowWidth();
    renderer.resolvedLayout_.windowHeight = renderer.WindowHeight();

    const RECT dashboardRect{
        renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
        renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
        renderer.WindowWidth() - renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
        renderer.WindowHeight() - renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin)
    };

    if (renderer.config_.layout.structure.cardsLayout.name.empty()) {
        renderer.lastError_ = "renderer:layout_missing_cards_root";
        return false;
    }

    renderer.WriteTrace("renderer:layout_begin window=" + std::to_string(renderer.resolvedLayout_.windowWidth) + "x" +
        std::to_string(renderer.resolvedLayout_.windowHeight) + " " + FormatRect(dashboardRect) +
        " cards_root=\"" + renderer.config_.layout.structure.cardsLayout.name + "\"");

    const auto resolveCard = [&](const LayoutNodeConfig& node, const RECT& rect) {
        const auto cardIt = std::find_if(renderer.config_.layout.cards.begin(), renderer.config_.layout.cards.end(), [&](const auto& card) {
            return card.id == node.name;
        });
        if (cardIt == renderer.config_.layout.cards.end()) {
            return;
        }

        DashboardRenderer::ResolvedCardLayout card;
        card.id = cardIt->id;
        card.title = cardIt->title;
        card.iconName = cardIt->icon;
        card.hasHeader = !card.title.empty() || !card.iconName.empty();
        card.rect = rect;

        const int padding = renderer.ScaleLogical(renderer.config_.layout.cardStyle.cardPadding);
        const int iconSize = renderer.ScaleLogical(renderer.config_.layout.cardStyle.headerIconSize);
        const int headerHeight = card.hasHeader ? renderer.EffectiveHeaderHeight() : 0;
        if (!card.iconName.empty()) {
            card.iconRect = RECT{
                card.rect.left + padding,
                card.rect.top + padding + (std::max)(0, (headerHeight - iconSize) / 2),
                card.rect.left + padding + iconSize,
                card.rect.top + padding + (std::max)(0, (headerHeight - iconSize) / 2) + iconSize
            };
        } else {
            card.iconRect = RECT{card.rect.left + padding, card.rect.top + padding, card.rect.left + padding, card.rect.top + padding};
        }
        const int titleLeft = !card.iconName.empty()
            ? card.iconRect.right + renderer.ScaleLogical(renderer.config_.layout.cardStyle.headerGap)
            : card.rect.left + padding;
        card.titleRect = RECT{
            titleLeft,
            card.rect.top + padding,
            card.rect.right - padding,
            card.rect.top + padding + headerHeight
        };
        card.contentRect = RECT{
            card.rect.left + padding,
            card.rect.top + padding + headerHeight + renderer.ScaleLogical(renderer.config_.layout.cardStyle.contentGap),
            card.rect.right - padding,
            card.rect.bottom - padding
        };

        renderer.WriteTrace("renderer:layout_card id=\"" + card.id + "\" " + FormatRect(card.rect) +
            " title=" + FormatRect(card.titleRect) +
            " icon=" + FormatRect(card.iconRect) +
            " content=" + FormatRect(card.contentRect));
        std::vector<std::string> cardReferenceStack;
        renderer.ResolveNodeWidgetsInternal(cardIt->layout, card.contentRect, card.widgets, cardReferenceStack, card.id, card.id, {});
        renderer.resolvedLayout_.cards.push_back(std::move(card));
    };

    std::function<void(const LayoutNodeConfig&, const RECT&, const std::vector<size_t>&)> resolveDashboardNode =
        [&](const LayoutNodeConfig& node, const RECT& rect, const std::vector<size_t>& nodePath) {
            if (!DashboardRenderer::IsContainerNode(node)) {
                resolveCard(node, rect);
                return;
            }

            const bool horizontal = node.name == "columns";
            const int gap = horizontal ? renderer.ScaleLogical(renderer.config_.layout.dashboard.cardGap)
                                       : renderer.ScaleLogical(renderer.config_.layout.dashboard.rowGap);
            int totalWeight = 0;
            for (const auto& child : node.children) {
                totalWeight += (std::max)(1, child.weight);
            }
            if (totalWeight <= 0) {
                return;
            }

            const int totalAvailable = (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
                gap * static_cast<int>((std::max)(static_cast<size_t>(0), node.children.size() - 1));
            int remainingAvailable = totalAvailable;
            int cursor = horizontal ? rect.left : rect.top;
            int remainingWeight = totalWeight;
            std::vector<RECT> childRects;
            childRects.reserve(node.children.size());
            for (size_t i = 0; i < node.children.size(); ++i) {
                const auto& child = node.children[i];
                const int childWeight = (std::max)(1, child.weight);
                const int size = (i + 1 == node.children.size())
                    ? ((horizontal ? rect.right : rect.bottom) - cursor)
                    : (std::max)(0, remainingAvailable * childWeight / (std::max)(1, remainingWeight));

                RECT childRect = rect;
                if (horizontal) {
                    childRect.left = cursor;
                    childRect.right = cursor + size;
                } else {
                    childRect.top = cursor;
                    childRect.bottom = cursor + size;
                }

                renderer.WriteTrace("renderer:layout_dashboard_child parent=\"" + node.name + "\" child=\"" + child.name +
                    "\" weight=" + std::to_string(childWeight) +
                    " gap=" + std::to_string(gap) +
                    " size=" + std::to_string(size) +
                    " " + FormatRect(childRect));
                childRects.push_back(childRect);
                std::vector<size_t> childPath = nodePath;
                childPath.push_back(i);
                resolveDashboardNode(child, childRect, childPath);
                cursor += size + gap;
                remainingAvailable -= size;
                remainingWeight -= childWeight;
            }
            renderer.AddLayoutEditGuide(node, rect, childRects, gap, "", "", nodePath);
        };

    resolveDashboardNode(renderer.config_.layout.structure.cardsLayout, dashboardRect, {});

    if (renderer.resolvedLayout_.cards.empty()) {
        renderer.lastError_ = "renderer:layout_resolve_failed cards=0 root=\"" + renderer.config_.layout.structure.cardsLayout.name + "\"";
        return false;
    }

    int gaugeCount = 0;
    int globalGaugeRadius = 0;
    for (const auto& card : renderer.resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.kind != DashboardRenderer::WidgetKind::Gauge) {
                continue;
            }

            const int gaugeRadius = renderer.GaugeRadiusForRect(widget.rect);
            if (gaugeCount == 0) {
                globalGaugeRadius = gaugeRadius;
            } else {
                globalGaugeRadius = (std::min)(globalGaugeRadius, gaugeRadius);
            }
            ++gaugeCount;
        }
    }
    renderer.resolvedLayout_.globalGaugeRadius = gaugeCount > 0 ? globalGaugeRadius :
        (std::max)(1, renderer.ScaleLogical(renderer.config_.layout.gauge.minRadius));
    renderer.WriteTrace("renderer:layout_global_gauge_radius count=" + std::to_string(gaugeCount) +
        " value=" + std::to_string(renderer.resolvedLayout_.globalGaugeRadius));

    BuildWidgetEditGuides(renderer);

    renderer.WriteTrace("renderer:layout_done cards=" + std::to_string(renderer.resolvedLayout_.cards.size()));
    return true;
}

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
        const int labelGap = (std::max)(0, renderer.ScaleLogical(renderer.config_.layout.driveUsageList.labelGap));
        const int activityWidth = (std::max)(1, renderer.ScaleLogical(renderer.config_.layout.driveUsageList.activityWidth));
        const int rwGap = (std::max)(0, renderer.ScaleLogical(renderer.config_.layout.driveUsageList.rwGap));
        const int barGap = (std::max)(0, renderer.ScaleLogical(renderer.config_.layout.driveUsageList.barGap));
        const int freeWidth = (std::max)(1, renderer.ScaleLogical(renderer.config_.layout.driveUsageList.freeWidth));
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
            (std::min)(widget.rect.right, static_cast<LONG>(labelRect.right + labelGap)),
            widget.rect.top,
            (std::min)(widget.rect.right, static_cast<LONG>(labelRect.right + labelGap + activityWidth)),
            widget.rect.bottom};
        RECT writeRect{
            (std::min)(widget.rect.right, static_cast<LONG>(readRect.right + rwGap)),
            widget.rect.top,
            (std::min)(widget.rect.right, static_cast<LONG>(readRect.right + rwGap + activityWidth)),
            widget.rect.bottom};
        RECT freeRect{
            (std::max)(widget.rect.left, static_cast<LONG>(widget.rect.right - freeWidth)),
            widget.rect.top,
            widget.rect.right,
            widget.rect.bottom};
        RECT pctRect{
            (std::max)(widget.rect.left, static_cast<LONG>(freeRect.left - percentWidth)),
            widget.rect.top,
            freeRect.left,
            widget.rect.bottom};
        RECT barRect{
            (std::min)(widget.rect.right, static_cast<LONG>(writeRect.right + barGap)),
            widget.rect.top,
            (std::max)((std::min)(widget.rect.right, static_cast<LONG>(writeRect.right + barGap)),
                static_cast<LONG>(pctRect.left - renderer.ScaleLogical(renderer.config_.layout.driveUsageList.percentGap))),
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

        addVerticalGuide(0, readRect.left, DashboardRenderer::WidgetEditParameter::DriveUsageLabelGap,
            renderer.config_.layout.driveUsageList.labelGap, 1);
        addVerticalGuide(1, writeRect.left, DashboardRenderer::WidgetEditParameter::DriveUsageRwGap,
            renderer.config_.layout.driveUsageList.rwGap, 1);
        addVerticalGuide(2, barRect.left, DashboardRenderer::WidgetEditParameter::DriveUsageBarGap,
            renderer.config_.layout.driveUsageList.barGap, 1);
        addVerticalGuide(3, barRect.right, DashboardRenderer::WidgetEditParameter::DriveUsagePercentGap,
            renderer.config_.layout.driveUsageList.percentGap, -1);
        addVerticalGuide(4, writeRect.right, DashboardRenderer::WidgetEditParameter::DriveUsageActivityWidth,
            renderer.config_.layout.driveUsageList.activityWidth, 1);
        addVerticalGuide(5, freeRect.left, DashboardRenderer::WidgetEditParameter::DriveUsageFreeWidth,
            renderer.config_.layout.driveUsageList.freeWidth, -1);
        addHorizontalGuide(6, widget.rect.top + headerHeight, DashboardRenderer::WidgetEditParameter::DriveUsageHeaderGap,
            renderer.config_.layout.driveUsageList.headerGap, 1);
        for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
            const int y = widget.rect.top + headerHeight + ((rowIndex + 1) * rowHeight);
            addHorizontalGuide(7 + rowIndex, y, DashboardRenderer::WidgetEditParameter::DriveUsageRowGap,
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
