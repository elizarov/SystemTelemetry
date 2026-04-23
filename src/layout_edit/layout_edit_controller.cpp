#include "layout_edit/layout_edit_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

#include "layout_edit/impl/layout_snap_solver.h"
#include "layout_edit/layout_edit_helpers.h"
#include "layout_edit/layout_edit_hit_priority.h"
#include "layout_edit/layout_edit_parameter_metadata.h"
#include "layout_edit/layout_edit_service.h"

namespace {

std::string FormatNodePath(const std::vector<size_t>& path) {
    std::string formatted;
    for (size_t i = 0; i < path.size(); ++i) {
        if (!formatted.empty()) {
            formatted += ".";
        }
        formatted += std::to_string(path[i]);
    }
    return formatted.empty() ? "root" : formatted;
}

const char* AxisName(LayoutGuideAxis axis) {
    return axis == LayoutGuideAxis::Vertical ? "vertical" : "horizontal";
}

std::string DescribeLayoutGuide(const LayoutEditGuide& guide) {
    std::string detail = "axis=" + std::string(AxisName(guide.axis)) +
                         " separator=" + std::to_string(guide.separatorIndex) +
                         " path=" + FormatNodePath(guide.nodePath);
    if (!guide.editCardId.empty()) {
        detail += " card=" + guide.editCardId;
    }
    return detail;
}

std::string DescribeWidgetParameter(LayoutEditParameter parameter) {
    const auto metadata = GetLayoutEditConfigFieldMetadata(parameter);
    return std::string(metadata.sectionName) + "." + std::string(metadata.parameterName);
}

std::string DescribeWidgetGuide(const LayoutEditWidgetGuide& guide) {
    return "axis=" + std::string(AxisName(guide.axis)) + " parameter=" + DescribeWidgetParameter(guide.parameter) +
           " guide_id=" + std::to_string(guide.guideId) +
           (guide.widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome
                   ? " card=" + guide.widget.editCardId
                   : " path=" + FormatNodePath(guide.widget.nodePath));
}

std::string DescribeGapEditAnchor(const LayoutEditGapAnchor& anchor) {
    const std::string scope = anchor.key.widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome
                                  ? " dashboard"
                                  : " card=" + anchor.key.widget.renderCardId;
    return "axis=" + std::string(AxisName(anchor.axis)) +
           " parameter=" + DescribeWidgetParameter(anchor.key.parameter) +
           " path=" + FormatNodePath(anchor.key.nodePath) + scope;
}

std::string DescribeEditableAnchor(const LayoutEditAnchorKey& key) {
    const std::string subject = LayoutEditAnchorParameter(key).has_value()
                                    ? "parameter=" + DescribeWidgetParameter(*LayoutEditAnchorParameter(key))
                                : LayoutEditAnchorMetricKey(key).has_value()
                                    ? "metric=" + LayoutEditAnchorMetricKey(key)->metricId
                                : LayoutEditAnchorMetricListOrderKey(key).has_value() ? "metric_list_reorder"
                                                                                      : "subject=unknown";
    return subject + " anchor_id=" + std::to_string(key.anchorId) +
           (key.widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome
                   ? " card=" + key.widget.editCardId
                   : " path=" + FormatNodePath(key.widget.nodePath));
}

double NormalizeDegrees(double degrees) {
    double normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    return normalized;
}

std::optional<double> ComputeGaugeSweepDegrees(RenderPoint origin, RenderPoint clientPoint) {
    const double dx = static_cast<double>(clientPoint.x - origin.x);
    const double dy = static_cast<double>(clientPoint.y - origin.y);
    if (std::abs(dx) < 0.001 && std::abs(dy) < 0.001) {
        return std::nullopt;
    }

    double endAngle = NormalizeDegrees(std::atan2(dy, dx) * 180.0 / 3.14159265358979323846);
    if (endAngle > 90.0 && endAngle < 270.0) {
        endAngle = NormalizeDegrees(540.0 - endAngle);
    }

    const double gapHalf = NormalizeDegrees(90.0 - endAngle);
    return std::clamp(360.0 - (gapHalf * 2.0), 0.0, 360.0);
}

std::optional<double> ComputeGaugePointerAngle(RenderPoint origin, RenderPoint clientPoint) {
    const double dx = static_cast<double>(clientPoint.x - origin.x);
    const double dy = static_cast<double>(clientPoint.y - origin.y);
    if (std::abs(dx) < 0.001 && std::abs(dy) < 0.001) {
        return std::nullopt;
    }
    return NormalizeDegrees(std::atan2(dy, dx) * 180.0 / 3.14159265358979323846);
}

double ClampAngleToRange(double angleDegrees, double angularMin, double angularMax) {
    double best = std::clamp(angleDegrees, angularMin, angularMax);
    double bestDistance = std::abs(best - angleDegrees);
    for (double candidate : {angleDegrees - 360.0, angleDegrees + 360.0}) {
        const double clamped = std::clamp(candidate, angularMin, angularMax);
        const double distance = std::abs(clamped - candidate);
        if (distance < bestDistance) {
            best = clamped;
            bestDistance = distance;
        }
    }
    return best;
}

double MinimumGaugeSegmentSweep(double totalSweep, int segmentCount) {
    if (totalSweep <= 0.0 || segmentCount <= 0) {
        return 0.0;
    }
    return (std::min)(0.25, totalSweep / static_cast<double>(segmentCount));
}

double ClampGaugeSegmentGapForCurrentConfig(const AppConfig& config, double value) {
    const double totalSweep = std::clamp(config.layout.gauge.sweepDegrees, 0.0, 360.0);
    const int segmentCount = (std::max)(1, config.layout.gauge.segmentCount);
    if (segmentCount <= 1) {
        return 0.0;
    }

    const double minSegmentSweep = MinimumGaugeSegmentSweep(totalSweep, segmentCount);
    const double maxSegmentGap = (std::max)(0.0,
        (totalSweep - (minSegmentSweep * static_cast<double>(segmentCount))) / static_cast<double>(segmentCount - 1));
    return std::clamp(value, 0.0, maxSegmentGap);
}

double ClampDriveUsageActivitySegmentGapForCurrentConfig(const AppConfig& config, double value) {
    const int segmentCount = (std::max)(1, config.layout.driveUsageList.activitySegments);
    if (segmentCount <= 1) {
        return 0.0;
    }

    const int rowContentHeight = (std::max)(config.layout.fonts.label.size,
        (std::max)(config.layout.fonts.smallText.size, config.layout.driveUsageList.barHeight));
    const int maxGap = (std::max)(0, (rowContentHeight - segmentCount) / (segmentCount - 1));
    return static_cast<double>(std::clamp((std::max)(0, static_cast<int>(std::lround(value))), 0, maxGap));
}

std::optional<double> ComputeGaugeSegmentGapDegrees(const LayoutEditWidgetGuide& guide, RenderPoint clientPoint) {
    const auto pointerAngle = ComputeGaugePointerAngle(guide.dragOrigin, clientPoint);
    if (!pointerAngle.has_value()) {
        return std::nullopt;
    }
    if (guide.guideId <= 0) {
        return 0.0;
    }
    const double clampedAngle = ClampAngleToRange(*pointerAngle, guide.angularMin, guide.angularMax);
    const double maxSegmentSweep = (std::max)(0.0, guide.angularMax - guide.angularMin);
    const double segmentSweep = std::clamp(clampedAngle - guide.angularMin, 0.0, maxSegmentSweep);
    const double segmentCount = static_cast<double>(guide.guideId + 1);
    const double segmentGap = (maxSegmentSweep - segmentSweep) * segmentCount / (segmentCount - 1.0);
    const double maxSegmentGap = maxSegmentSweep * segmentCount / (segmentCount - 1.0);
    return std::clamp(segmentGap, 0.0, maxSegmentGap);
}

}  // namespace

LayoutEditHost::LayoutTarget LayoutEditHost::LayoutTarget::ForGuide(const LayoutEditGuide& guide) {
    LayoutTarget target;
    target.editCardId = guide.editCardId;
    target.nodePath = guide.nodePath;
    return target;
}

LayoutEditController::LayoutEditController(LayoutEditHost& host) : host_(host) {}

void LayoutEditController::StartSession() {
    host_.LayoutDashboardOverlayState().showLayoutEditGuides = true;
    ClearInteractionState();
    SyncRendererInteractionState();
}

void LayoutEditController::StopSession(bool showLayoutEditGuidesAfterStop) {
    ClearInteractionState();
    host_.LayoutDashboardOverlayState().showLayoutEditGuides = showLayoutEditGuidesAfterStop;
    SyncRendererInteractionState();
    ReleaseCapture();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    host_.EndLayoutEditTraceSession("session_stop");
    host_.InvalidateLayoutEdit();
}

const LayoutEditGuide* LayoutEditController::HitTestLayoutGuide(RenderPoint clientPoint, size_t* index) const {
    const auto& guides = host_.LayoutEditRenderer().LayoutEditGuides();
    for (size_t i = 0; i < guides.size(); ++i) {
        if (guides[i].hitRect.Contains(clientPoint)) {
            if (index != nullptr) {
                *index = i;
            }
            return &guides[i];
        }
    }
    return nullptr;
}

const LayoutEditWidgetGuide* LayoutEditController::HitTestWidgetEditGuide(
    RenderPoint clientPoint, size_t* index) const {
    const auto& guides = host_.LayoutEditRenderer().WidgetEditGuides();
    const LayoutEditWidgetGuide* bestGuide = nullptr;
    size_t bestIndex = 0;
    int bestPriority = (std::numeric_limits<int>::max)();
    for (size_t i = 0; i < guides.size(); ++i) {
        if (!guides[i].hitRect.Contains(clientPoint)) {
            continue;
        }

        const int priority = GetLayoutEditParameterHitPriority(guides[i].parameter);
        if (bestGuide == nullptr || priority < bestPriority) {
            bestGuide = &guides[i];
            bestIndex = i;
            bestPriority = priority;
        }
    }
    if (bestGuide != nullptr && index != nullptr) {
        *index = bestIndex;
    }
    return bestGuide;
}

const LayoutEditGapAnchor* LayoutEditController::HitTestGapEditAnchor(RenderPoint clientPoint, size_t* index) const {
    const auto& anchors = host_.LayoutEditRenderer().GapEditAnchors();
    const LayoutEditGapAnchor* bestAnchor = nullptr;
    size_t bestIndex = 0;
    int bestPriority = (std::numeric_limits<int>::max)();
    for (size_t i = 0; i < anchors.size(); ++i) {
        if (!anchors[i].hitRect.Contains(clientPoint)) {
            continue;
        }

        const int priority = GetLayoutEditParameterHitPriority(anchors[i].key.parameter);
        if (bestAnchor == nullptr || priority < bestPriority) {
            bestAnchor = &anchors[i];
            bestIndex = i;
            bestPriority = priority;
        }
    }
    if (bestAnchor != nullptr && index != nullptr) {
        *index = bestIndex;
    }
    return bestAnchor;
}

LayoutEditController::HoverResolution LayoutEditController::ResolveHover(RenderPoint clientPoint) const {
    HoverResolution resolution;
    DashboardRenderer& renderer = host_.LayoutEditRenderer();
    resolution.hoveredLayoutCard = renderer.HitTestLayoutCard(clientPoint);
    resolution.hoveredEditableCard = renderer.HitTestEditableCard(clientPoint);

    const std::optional<LayoutEditAnchorKey> anchorHandle = renderer.HitTestEditableAnchorHandle(clientPoint);
    const std::optional<LayoutEditAnchorRegion> anchorRegion =
        anchorHandle.has_value() ? renderer.FindEditableAnchorRegion(*anchorHandle) : std::nullopt;
    const auto setHoveredAnchor = [&]() {
        resolution.hoveredEditableAnchor = anchorHandle;
        if (anchorHandle->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = anchorHandle->widget;
        }
    };
    const auto setActionableAnchor = [&]() {
        setHoveredAnchor();
        if (anchorRegion.has_value() && anchorRegion->draggable) {
            resolution.actionableAnchorHandle = anchorHandle;
        }
    };
    size_t gapAnchorIndex = 0;
    const LayoutEditGapAnchor* gapAnchor = HitTestGapEditAnchor(clientPoint, &gapAnchorIndex);
    size_t widgetGuideIndex = 0;
    const LayoutEditWidgetGuide* widgetGuide = HitTestWidgetEditGuide(clientPoint, &widgetGuideIndex);
    if (anchorHandle.has_value() && gapAnchor != nullptr) {
        const int anchorPriority = LayoutEditAnchorHitPriority(*anchorHandle);
        const int gapPriority = GetLayoutEditParameterHitPriority(gapAnchor->key.parameter);
        if (anchorPriority <= gapPriority) {
            setActionableAnchor();
            return resolution;
        }

        resolution.hoveredGapEditAnchor = gapAnchor->key;
        resolution.hoveredGapEditAnchorIndex = gapAnchorIndex;
        resolution.actionableGapEditAnchor = gapAnchor->key;
        return resolution;
    }
    if (anchorHandle.has_value() && widgetGuide != nullptr) {
        const int anchorPriority = LayoutEditAnchorHitPriority(*anchorHandle);
        const int guidePriority = GetLayoutEditParameterHitPriority(widgetGuide->parameter);
        if (anchorPriority <= guidePriority) {
            setActionableAnchor();
            return resolution;
        }

        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuideIndex = widgetGuideIndex;
        return resolution;
    }

    if (gapAnchor != nullptr && widgetGuide != nullptr) {
        const int gapPriority = GetLayoutEditParameterHitPriority(gapAnchor->key.parameter);
        const int guidePriority = GetLayoutEditParameterHitPriority(widgetGuide->parameter);
        if (gapPriority <= guidePriority) {
            resolution.hoveredGapEditAnchor = gapAnchor->key;
            resolution.hoveredGapEditAnchorIndex = gapAnchorIndex;
            resolution.actionableGapEditAnchor = gapAnchor->key;
            return resolution;
        }

        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuideIndex = widgetGuideIndex;
        return resolution;
    }

    if (anchorHandle.has_value()) {
        setActionableAnchor();
        return resolution;
    }

    if (gapAnchor != nullptr) {
        resolution.hoveredGapEditAnchor = gapAnchor->key;
        resolution.hoveredGapEditAnchorIndex = gapAnchorIndex;
        resolution.actionableGapEditAnchor = gapAnchor->key;
        return resolution;
    }

    if (widgetGuide != nullptr) {
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuideIndex = widgetGuideIndex;
        return resolution;
    }

    const std::optional<LayoutEditAnchorKey> anchorTarget = renderer.HitTestEditableAnchorTarget(clientPoint);
    const std::optional<LayoutEditWidgetIdentity> hoveredWidget = renderer.HitTestEditableWidget(clientPoint);
    if (hoveredWidget.has_value()) {
        resolution.hoveredEditableWidget = hoveredWidget;

        if (anchorTarget.has_value() && MatchesWidgetIdentity(anchorTarget->widget, *hoveredWidget)) {
            resolution.hoveredEditableAnchor = anchorTarget;
            return resolution;
        }

        return resolution;
    }

    if (anchorTarget.has_value()) {
        resolution.hoveredEditableAnchor = anchorTarget;
        return resolution;
    }

    size_t layoutGuideIndex = 0;
    if (HitTestLayoutGuide(clientPoint, &layoutGuideIndex) != nullptr) {
        resolution.hoveredLayoutGuideIndex = layoutGuideIndex;
        return resolution;
    }

    return resolution;
}

void LayoutEditController::RefreshHover(RenderPoint clientPoint) {
    if (activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeGapEditDrag_.has_value() ||
        activeAnchorEditDrag_.has_value()) {
        return;
    }

    const HoverResolution resolution = ResolveHover(clientPoint);
    const std::optional<LayoutEditWidgetIdentity>& nextHoveredLayoutCard = resolution.hoveredLayoutCard;
    const std::optional<LayoutEditWidgetIdentity>& nextHoveredCard = resolution.hoveredEditableCard;
    const std::optional<LayoutEditWidgetIdentity>& nextHoveredWidget = resolution.hoveredEditableWidget;
    const std::optional<LayoutEditGapAnchorKey>& nextHoveredGapAnchor = resolution.hoveredGapEditAnchor;
    const std::optional<LayoutEditAnchorKey>& nextHoveredAnchor = resolution.hoveredEditableAnchor;

    bool hoverChanged = (hoveredLayoutCard_.has_value() != nextHoveredLayoutCard.has_value());
    if (!hoverChanged && hoveredLayoutCard_.has_value() && nextHoveredLayoutCard.has_value()) {
        hoverChanged = !MatchesWidgetIdentity(*hoveredLayoutCard_, *nextHoveredLayoutCard);
    }
    if (hoverChanged) {
        hoveredLayoutCard_ = nextHoveredLayoutCard;
        host_.LayoutDashboardOverlayState().hoveredLayoutCard = hoveredLayoutCard_;
    }

    bool cardHoverChanged = (hoveredEditableCard_.has_value() != nextHoveredCard.has_value());
    if (!cardHoverChanged && hoveredEditableCard_.has_value() && nextHoveredCard.has_value()) {
        cardHoverChanged = !MatchesWidgetIdentity(*hoveredEditableCard_, *nextHoveredCard);
    }
    if (cardHoverChanged) {
        hoveredEditableCard_ = nextHoveredCard;
        host_.LayoutDashboardOverlayState().hoveredEditableCard = hoveredEditableCard_;
    }
    hoverChanged = hoverChanged || cardHoverChanged;

    bool widgetHoverChanged = (hoveredEditableWidget_.has_value() != nextHoveredWidget.has_value());
    if (!widgetHoverChanged && hoveredEditableWidget_.has_value() && nextHoveredWidget.has_value()) {
        widgetHoverChanged = !MatchesWidgetIdentity(*hoveredEditableWidget_, *nextHoveredWidget);
    }
    if (widgetHoverChanged) {
        hoveredEditableWidget_ = nextHoveredWidget;
        host_.LayoutDashboardOverlayState().hoveredEditableWidget = hoveredEditableWidget_;
    }
    hoverChanged = hoverChanged || widgetHoverChanged;

    if (hoveredGapEditAnchor_.has_value() != nextHoveredGapAnchor.has_value() ||
        (hoveredGapEditAnchor_.has_value() && nextHoveredGapAnchor.has_value() &&
            !MatchesGapEditAnchorKey(*hoveredGapEditAnchor_, *nextHoveredGapAnchor))) {
        hoveredGapEditAnchor_ = nextHoveredGapAnchor;
        host_.LayoutDashboardOverlayState().hoveredGapEditAnchor = hoveredGapEditAnchor_;
        hoverChanged = true;
    }

    const std::optional<size_t>& nextGapAnchorIndex = resolution.hoveredGapEditAnchorIndex;
    if (hoveredGapEditAnchorIndex_ != nextGapAnchorIndex) {
        hoveredGapEditAnchorIndex_ = nextGapAnchorIndex;
        hoverChanged = true;
    }

    if (hoveredEditableAnchor_.has_value() != nextHoveredAnchor.has_value() ||
        (hoveredEditableAnchor_.has_value() && nextHoveredAnchor.has_value() &&
            !MatchesEditableAnchorKey(*hoveredEditableAnchor_, *nextHoveredAnchor))) {
        hoveredEditableAnchor_ = nextHoveredAnchor;
        host_.LayoutDashboardOverlayState().hoveredEditableAnchor = hoveredEditableAnchor_;
        hoverChanged = true;
    }

    const std::optional<size_t>& nextWidgetGuideIndex = resolution.hoveredWidgetEditGuideIndex;
    if (hoveredWidgetEditGuideIndex_ != nextWidgetGuideIndex) {
        hoveredWidgetEditGuideIndex_ = nextWidgetGuideIndex;
        hoverChanged = true;
    }

    const std::optional<size_t>& nextLayoutGuideIndex = resolution.hoveredLayoutGuideIndex;
    if (hoveredLayoutGuideIndex_ != nextLayoutGuideIndex) {
        hoveredLayoutGuideIndex_ = nextLayoutGuideIndex;
        host_.LayoutDashboardOverlayState().hoveredLayoutEditGuide =
            hoveredLayoutGuideIndex_.has_value()
                ? std::optional<LayoutEditGuide>(
                      host_.LayoutEditRenderer().LayoutEditGuides()[*hoveredLayoutGuideIndex_])
                : std::nullopt;
        hoverChanged = true;
    }

    if (hoverChanged) {
        host_.InvalidateLayoutEdit();
    }

    SetCursorForPoint(clientPoint);
}

bool LayoutEditController::HandleLButtonDown(HWND hwnd, RenderPoint clientPoint) {
    lastClientPoint_ = clientPoint;
    DashboardRenderer& renderer = host_.LayoutEditRenderer();
    const HoverResolution resolution = ResolveHover(clientPoint);
    hoveredLayoutCard_ = resolution.hoveredLayoutCard;
    hoveredEditableCard_ = resolution.hoveredEditableCard;
    hoveredEditableWidget_ = resolution.hoveredEditableWidget;
    hoveredGapEditAnchor_ = resolution.hoveredGapEditAnchor;
    hoveredEditableAnchor_ = resolution.hoveredEditableAnchor;
    if (resolution.actionableAnchorHandle.has_value()) {
        const auto region = renderer.FindEditableAnchorRegion(*resolution.actionableAnchorHandle);
        if (region.has_value()) {
            if (LayoutEditAnchorMetricListOrderKey(region->key).has_value()) {
                const LayoutNodeConfig* node = FindEditableWidgetNode(host_.LayoutEditConfig(), region->key.widget);
                if (node == nullptr || node->name != "metric_list") {
                    return false;
                }

                std::vector<std::string> metricRefs;
                std::stringstream stream(node->parameter);
                std::string item;
                while (std::getline(stream, item, ',')) {
                    if (!item.empty()) {
                        metricRefs.push_back(item);
                    }
                }
                if (region->key.anchorId < 0 || region->key.anchorId >= static_cast<int>(metricRefs.size())) {
                    return false;
                }

                const int rowHeight =
                    (std::max)(1, static_cast<int>(region->targetRect.bottom - region->targetRect.top));
                const int rowTop = static_cast<int>(region->targetRect.top) - (region->key.anchorId * rowHeight);
                activeMetricListReorderDrag_ = MetricListReorderDragState{region->key.widget,
                    metricRefs,
                    rowTop,
                    rowHeight,
                    static_cast<int>(metricRefs.size()),
                    region->key.anchorId};
                hoveredEditableWidget_ = region->key.widget;
                renderer.SetInteractiveDragTraceActive(true);
                host_.BeginLayoutEditTraceSession("metric_list_reorder", DescribeEditableAnchor(region->key));
                SyncRendererInteractionState();
                SetCapture(hwnd);
                return true;
            }
            const double startDx = static_cast<double>(clientPoint.x - region->dragOrigin.x);
            const double startDy = static_cast<double>(clientPoint.y - region->dragOrigin.y);
            activeAnchorEditDrag_ = AnchorEditDragState{region->key,
                region->dragAxis,
                region->dragMode,
                region->dragOrigin,
                region->dragScale,
                region->value,
                clientPoint,
                std::sqrt((startDx * startDx) + (startDy * startDy))};
            if (region->key.widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
                hoveredEditableCard_ = region->key.widget;
                hoveredEditableWidget_.reset();
            } else {
                hoveredEditableWidget_ = region->key.widget;
            }
            renderer.SetInteractiveDragTraceActive(true);
            host_.BeginLayoutEditTraceSession("anchor", DescribeEditableAnchor(region->key));
            SyncRendererInteractionState();
            SetCapture(hwnd);
            return true;
        }
    }

    if (resolution.actionableGapEditAnchor.has_value()) {
        const auto anchor = renderer.FindGapEditAnchor(*resolution.actionableGapEditAnchor);
        if (anchor.has_value()) {
            activeGapEditDrag_ = GapEditDragState{
                *anchor,
                anchor->value,
                anchor->dragAxis == AnchorDragAxis::Horizontal ? clientPoint.x : clientPoint.y,
            };
            hoveredGapEditAnchorIndex_ = resolution.hoveredGapEditAnchorIndex;
            renderer.SetInteractiveDragTraceActive(true);
            host_.BeginLayoutEditTraceSession("gap_anchor", DescribeGapEditAnchor(*anchor));
            SyncRendererInteractionState();
            SetCapture(hwnd);
            return true;
        }
    }

    if (resolution.hoveredWidgetEditGuideIndex.has_value()) {
        const auto& guides = renderer.WidgetEditGuides();
        const LayoutEditWidgetGuide& widgetGuide = guides[*resolution.hoveredWidgetEditGuideIndex];
        activeWidgetEditDrag_ = WidgetEditDragState{
            widgetGuide,
            widgetGuide.value,
            widgetGuide.axis == LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y,
        };
        if (widgetGuide.widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            hoveredEditableCard_ = widgetGuide.widget;
            hoveredEditableWidget_.reset();
        } else {
            hoveredEditableWidget_ = widgetGuide.widget;
        }
        hoveredWidgetEditGuideIndex_ = resolution.hoveredWidgetEditGuideIndex;
        renderer.SetInteractiveDragTraceActive(true);
        host_.BeginLayoutEditTraceSession("widget_guide", DescribeWidgetGuide(widgetGuide));
        SyncRendererInteractionState();
        SetCapture(hwnd);
        return true;
    }

    if (resolution.hoveredLayoutGuideIndex.has_value()) {
        const auto& guides = renderer.LayoutEditGuides();
        const LayoutEditGuide& guide = guides[*resolution.hoveredLayoutGuideIndex];
        const LayoutNodeConfig* guideNode =
            FindGuideNode(host_.LayoutEditConfig(), LayoutEditHost::LayoutTarget::ForGuide(guide));
        const std::vector<int> initialWeights = SeedGuideWeights(guide, guideNode);
        activeLayoutDrag_ = LayoutDragState{
            guide,
            initialWeights,
            renderer.CollectLayoutGuideSnapCandidates(guide),
            guide.axis == LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y,
        };
        renderer.SetLayoutGuideDragActive(true);
        renderer.SetInteractiveDragTraceActive(true);
        hoveredLayoutGuideIndex_ = resolution.hoveredLayoutGuideIndex;
        host_.BeginLayoutEditTraceSession("layout_guide", DescribeLayoutGuide(guide));
        SyncRendererInteractionState();
        SetCapture(hwnd);
        return true;
    }

    return false;
}

bool LayoutEditController::HandleMouseMove(RenderPoint clientPoint) {
    lastClientPoint_ = clientPoint;
    host_.LayoutDashboardOverlayState().hoverOnExposedDashboard = true;
    if (activeLayoutDrag_.has_value()) {
        return UpdateLayoutDrag(clientPoint);
    }
    if (activeMetricListReorderDrag_.has_value()) {
        return UpdateMetricListReorderDrag(clientPoint);
    }
    if (activeGapEditDrag_.has_value()) {
        return UpdateGapEditDrag(clientPoint);
    }
    if (activeAnchorEditDrag_.has_value()) {
        return UpdateAnchorEditDrag(clientPoint);
    }
    if (activeWidgetEditDrag_.has_value()) {
        return UpdateWidgetEditDrag(clientPoint);
    }

    RefreshHover(clientPoint);
    return true;
}

bool LayoutEditController::HandleMouseLeave() {
    if (activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeGapEditDrag_.has_value() ||
        activeAnchorEditDrag_.has_value() || activeMetricListReorderDrag_.has_value()) {
        return false;
    }

    host_.LayoutDashboardOverlayState().hoverOnExposedDashboard = false;
    lastClientPoint_.reset();

    const bool hadHover = hoveredLayoutGuideIndex_.has_value() || hoveredLayoutCard_.has_value() ||
                          hoveredEditableCard_.has_value() || hoveredEditableWidget_.has_value() ||
                          hoveredGapEditAnchorIndex_.has_value() || hoveredGapEditAnchor_.has_value() ||
                          hoveredWidgetEditGuideIndex_.has_value() || hoveredEditableAnchor_.has_value();
    if (!hadHover) {
        return false;
    }

    hoveredLayoutGuideIndex_.reset();
    hoveredLayoutCard_.reset();
    hoveredEditableCard_.reset();
    hoveredEditableWidget_.reset();
    hoveredGapEditAnchorIndex_.reset();
    hoveredGapEditAnchor_.reset();
    hoveredWidgetEditGuideIndex_.reset();
    hoveredEditableAnchor_.reset();
    SyncRendererInteractionState();
    host_.InvalidateLayoutEdit();
    return true;
}

bool LayoutEditController::HandleLButtonUp(RenderPoint clientPoint) {
    lastClientPoint_ = clientPoint;
    bool released = false;
    bool releasedLayoutDrag = false;
    if (activeAnchorEditDrag_.has_value()) {
        activeAnchorEditDrag_.reset();
        released = true;
    } else if (activeMetricListReorderDrag_.has_value()) {
        activeMetricListReorderDrag_.reset();
        released = true;
    } else if (activeGapEditDrag_.has_value()) {
        activeGapEditDrag_.reset();
        released = true;
    } else if (activeWidgetEditDrag_.has_value()) {
        activeWidgetEditDrag_.reset();
        released = true;
    } else if (activeLayoutDrag_.has_value()) {
        activeLayoutDrag_.reset();
        released = true;
        releasedLayoutDrag = true;
    }

    if (!released) {
        return false;
    }

    if (releasedLayoutDrag) {
        host_.LayoutEditRenderer().SetLayoutGuideDragActive(false);
        host_.LayoutEditRenderer().RebuildEditArtifacts();
    }
    host_.LayoutEditRenderer().SetInteractiveDragTraceActive(false);
    SyncRendererInteractionState();
    ReleaseCapture();
    RefreshHover(clientPoint);
    host_.EndLayoutEditTraceSession("mouse_up");
    return true;
}

bool LayoutEditController::HandleCaptureChanged(HWND hwnd, HWND newCaptureOwner) {
    if (newCaptureOwner == hwnd) {
        return false;
    }

    const bool hadActiveDrag = activeAnchorEditDrag_.has_value() || activeMetricListReorderDrag_.has_value() ||
                               activeGapEditDrag_.has_value() || activeWidgetEditDrag_.has_value() ||
                               activeLayoutDrag_.has_value();
    if (!hadActiveDrag) {
        return false;
    }

    activeAnchorEditDrag_.reset();
    activeMetricListReorderDrag_.reset();
    activeGapEditDrag_.reset();
    activeWidgetEditDrag_.reset();
    const bool hadLayoutDrag = activeLayoutDrag_.has_value();
    activeLayoutDrag_.reset();
    if (hadLayoutDrag) {
        host_.LayoutEditRenderer().SetLayoutGuideDragActive(false);
        host_.LayoutEditRenderer().RebuildEditArtifacts();
    }
    host_.LayoutEditRenderer().SetInteractiveDragTraceActive(false);
    SyncRendererInteractionState();
    host_.InvalidateLayoutEdit();
    host_.EndLayoutEditTraceSession("capture_changed");
    return true;
}

bool LayoutEditController::HandleSetCursor(HWND hwnd) {
    if (activeAnchorEditDrag_.has_value()) {
        SetCursor(LoadCursorW(nullptr,
            activeAnchorEditDrag_->dragMode == AnchorDragMode::RadialDistance ? IDC_SIZEALL
            : activeAnchorEditDrag_->dragAxis == AnchorDragAxis::Both         ? IDC_SIZEALL
            : activeAnchorEditDrag_->dragAxis == AnchorDragAxis::Vertical     ? IDC_SIZEWE
                                                                              : IDC_SIZENS));
        return true;
    }
    if (activeMetricListReorderDrag_.has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
        return true;
    }
    if (activeGapEditDrag_.has_value()) {
        SetCursor(LoadCursorW(
            nullptr, activeGapEditDrag_->anchor.dragAxis == AnchorDragAxis::Horizontal ? IDC_SIZEWE : IDC_SIZENS));
        return true;
    }
    if (activeWidgetEditDrag_.has_value()) {
        const auto& guide = activeWidgetEditDrag_->guide;
        SetCursor(LoadCursorW(nullptr,
            guide.angularDrag                         ? IDC_CROSS
            : guide.axis == LayoutGuideAxis::Vertical ? IDC_SIZEWE
                                                      : IDC_SIZENS));
        return true;
    }
    if (activeLayoutDrag_.has_value()) {
        const auto& guide = activeLayoutDrag_->guide;
        SetCursor(LoadCursorW(nullptr, guide.axis == LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
        return true;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);
    RefreshHover(RenderPoint{cursor.x, cursor.y});
    return true;
}

bool LayoutEditController::HasActiveDrag() const {
    return activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeGapEditDrag_.has_value() ||
           activeAnchorEditDrag_.has_value() || activeMetricListReorderDrag_.has_value();
}

void LayoutEditController::CancelInteraction() {
    const bool hadInteraction =
        hoveredLayoutGuideIndex_.has_value() || hoveredLayoutCard_.has_value() || hoveredEditableCard_.has_value() ||
        hoveredEditableWidget_.has_value() || hoveredGapEditAnchorIndex_.has_value() ||
        hoveredGapEditAnchor_.has_value() || hoveredWidgetEditGuideIndex_.has_value() ||
        hoveredEditableAnchor_.has_value() || activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() ||
        activeGapEditDrag_.has_value() || activeAnchorEditDrag_.has_value() || activeMetricListReorderDrag_.has_value();
    const bool hadLayoutDrag = activeLayoutDrag_.has_value();
    if (!hadInteraction) {
        return;
    }

    ClearInteractionState();
    if (hadLayoutDrag) {
        host_.LayoutEditRenderer().RebuildEditArtifacts();
    }
    SyncRendererInteractionState();
    ReleaseCapture();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    host_.EndLayoutEditTraceSession("modal_ui");
    host_.InvalidateLayoutEdit();
}

std::optional<LayoutEditController::TooltipTarget> LayoutEditController::CurrentTooltipTarget() {
    if (!lastClientPoint_.has_value()) {
        return std::nullopt;
    }
    const RenderPoint clientPoint = *lastClientPoint_;

    if (activeLayoutDrag_.has_value()) {
        return TooltipTarget{clientPoint, activeLayoutDrag_->guide};
    }

    DashboardRenderer& renderer = host_.LayoutEditRenderer();
    if (activeMetricListReorderDrag_.has_value()) {
        const LayoutEditAnchorKey key{activeMetricListReorderDrag_->widget,
            LayoutMetricListOrderEditKey{
                activeMetricListReorderDrag_->widget.editCardId, activeMetricListReorderDrag_->widget.nodePath},
            activeMetricListReorderDrag_->currentIndex};
        const auto region = renderer.FindEditableAnchorRegion(key);
        if (region.has_value()) {
            return TooltipTarget{clientPoint, *region};
        }
    }
    if (activeGapEditDrag_.has_value()) {
        const auto anchor = renderer.FindGapEditAnchor(activeGapEditDrag_->anchor.key);
        if (anchor.has_value()) {
            return TooltipTarget{clientPoint, *anchor};
        }
    }

    if (activeWidgetEditDrag_.has_value()) {
        return TooltipTarget{clientPoint, activeWidgetEditDrag_->guide};
    }

    if (activeAnchorEditDrag_.has_value()) {
        const auto region = renderer.FindEditableAnchorRegion(activeAnchorEditDrag_->key);
        if (region.has_value()) {
            return TooltipTarget{clientPoint, *region};
        }
    }

    const HoverResolution resolution = ResolveHover(clientPoint);
    if (resolution.actionableGapEditAnchor.has_value()) {
        const auto anchor = renderer.FindGapEditAnchor(*resolution.actionableGapEditAnchor);
        if (anchor.has_value()) {
            return TooltipTarget{clientPoint, *anchor};
        }
    }

    if (resolution.actionableAnchorHandle.has_value()) {
        const auto region = renderer.FindEditableAnchorRegion(*resolution.actionableAnchorHandle);
        if (region.has_value()) {
            return TooltipTarget{clientPoint, *region};
        }
    }

    if (const auto anchorHandle = renderer.HitTestEditableAnchorHandle(clientPoint); anchorHandle.has_value()) {
        const auto region = renderer.FindEditableAnchorRegion(*anchorHandle);
        if (region.has_value() && !region->draggable) {
            return TooltipTarget{clientPoint, *region};
        }
    }

    if (resolution.hoveredWidgetEditGuideIndex.has_value()) {
        const auto& guides = renderer.WidgetEditGuides();
        if (*resolution.hoveredWidgetEditGuideIndex < guides.size()) {
            return TooltipTarget{clientPoint, guides[*resolution.hoveredWidgetEditGuideIndex]};
        }
    }

    if (resolution.hoveredLayoutGuideIndex.has_value()) {
        const auto& guides = renderer.LayoutEditGuides();
        if (*resolution.hoveredLayoutGuideIndex < guides.size()) {
            return TooltipTarget{clientPoint, guides[*resolution.hoveredLayoutGuideIndex]};
        }
    }

    if (const auto colorRegion = renderer.HitTestEditableColorRegion(clientPoint); colorRegion.has_value()) {
        return TooltipTarget{clientPoint, *colorRegion};
    }

    return std::nullopt;
}

void LayoutEditController::SyncRendererInteractionState() {
    DashboardOverlayState& overlayState = host_.LayoutDashboardOverlayState();
    overlayState.hoveredLayoutEditGuide =
        hoveredLayoutGuideIndex_.has_value()
            ? std::optional<LayoutEditGuide>(host_.LayoutEditRenderer().LayoutEditGuides()[*hoveredLayoutGuideIndex_])
            : std::nullopt;
    overlayState.hoveredLayoutCard = hoveredLayoutCard_;
    overlayState.hoveredEditableCard = hoveredEditableCard_;
    overlayState.hoveredEditableWidget = hoveredEditableWidget_;
    overlayState.hoveredGapEditAnchor = hoveredGapEditAnchor_;
    overlayState.hoveredEditableAnchor = hoveredEditableAnchor_;
    overlayState.activeLayoutEditGuide =
        activeLayoutDrag_.has_value() ? std::optional<LayoutEditGuide>(activeLayoutDrag_->guide) : std::nullopt;
    overlayState.activeWidgetEditGuide = activeWidgetEditDrag_.has_value()
                                             ? std::optional<LayoutEditWidgetGuide>(activeWidgetEditDrag_->guide)
                                             : std::nullopt;
    overlayState.activeGapEditAnchor = activeGapEditDrag_.has_value()
                                           ? std::optional<LayoutEditGapAnchorKey>(activeGapEditDrag_->anchor.key)
                                           : std::nullopt;
    overlayState.activeEditableAnchor = activeAnchorEditDrag_.has_value()
                                            ? std::optional<LayoutEditAnchorKey>(activeAnchorEditDrag_->key)
                                            : std::nullopt;
    if (activeMetricListReorderDrag_.has_value()) {
        overlayState.activeEditableAnchor = LayoutEditAnchorKey{activeMetricListReorderDrag_->widget,
            LayoutMetricListOrderEditKey{
                activeMetricListReorderDrag_->widget.editCardId, activeMetricListReorderDrag_->widget.nodePath},
            activeMetricListReorderDrag_->currentIndex};
    }
    overlayState.hoverOnExposedDashboard = overlayState.hoverOnExposedDashboard || HasActiveDrag();
}

void LayoutEditController::ClearInteractionState() {
    lastClientPoint_.reset();
    hoveredLayoutGuideIndex_.reset();
    hoveredLayoutCard_.reset();
    hoveredEditableCard_.reset();
    hoveredEditableWidget_.reset();
    hoveredGapEditAnchorIndex_.reset();
    hoveredGapEditAnchor_.reset();
    hoveredWidgetEditGuideIndex_.reset();
    hoveredEditableAnchor_.reset();
    host_.LayoutEditRenderer().SetLayoutGuideDragActive(false);
    host_.LayoutEditRenderer().SetInteractiveDragTraceActive(false);
    activeLayoutDrag_.reset();
    activeWidgetEditDrag_.reset();
    activeGapEditDrag_.reset();
    activeAnchorEditDrag_.reset();
    activeMetricListReorderDrag_.reset();
    host_.LayoutDashboardOverlayState().hoverOnExposedDashboard = false;
}

void LayoutEditController::SetCursorForPoint(RenderPoint clientPoint) {
    const HoverResolution resolution = ResolveHover(clientPoint);
    if (resolution.actionableGapEditAnchor.has_value()) {
        const auto anchor = host_.LayoutEditRenderer().FindGapEditAnchor(*resolution.actionableGapEditAnchor);
        const auto dragAxis = anchor.has_value() ? anchor->dragAxis : AnchorDragAxis::Vertical;
        SetCursor(LoadCursorW(nullptr, dragAxis == AnchorDragAxis::Horizontal ? IDC_SIZEWE : IDC_SIZENS));
        return;
    }

    if (resolution.actionableAnchorHandle.has_value()) {
        const auto region = host_.LayoutEditRenderer().FindEditableAnchorRegion(*resolution.actionableAnchorHandle);
        const auto dragAxis = region.has_value() ? region->dragAxis : AnchorDragAxis::Vertical;
        const auto dragMode = region.has_value() ? region->dragMode : AnchorDragMode::AxisDelta;
        SetCursor(LoadCursorW(nullptr,
            dragMode == AnchorDragMode::RadialDistance ? IDC_SIZEALL
            : dragAxis == AnchorDragAxis::Both         ? IDC_SIZEALL
            : dragAxis == AnchorDragAxis::Vertical     ? IDC_SIZEWE
                                                       : IDC_SIZENS));
        return;
    }

    if (resolution.hoveredWidgetEditGuideIndex.has_value()) {
        const auto& guides = host_.LayoutEditRenderer().WidgetEditGuides();
        const LayoutEditWidgetGuide& widgetGuide = guides[*resolution.hoveredWidgetEditGuideIndex];
        SetCursor(LoadCursorW(nullptr,
            widgetGuide.angularDrag                         ? IDC_CROSS
            : widgetGuide.axis == LayoutGuideAxis::Vertical ? IDC_SIZEWE
                                                            : IDC_SIZENS));
        return;
    }

    if (resolution.hoveredLayoutGuideIndex.has_value()) {
        const auto& guides = host_.LayoutEditRenderer().LayoutEditGuides();
        const LayoutEditGuide& layoutGuide = guides[*resolution.hoveredLayoutGuideIndex];
        SetCursor(LoadCursorW(nullptr, layoutGuide.axis == LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
        return;
    }

    if (host_.LayoutEditRenderer().HitTestEditableColorRegion(clientPoint).has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        return;
    }

    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

std::optional<std::vector<int>> LayoutEditController::FindSnappedLayoutGuideWeights(
    LayoutDragState& drag, const std::vector<int>& freeWeights) {
    const int threshold = host_.LayoutEditRenderer().LayoutSimilarityThreshold();
    if (threshold <= 0 || drag.snapCandidates.empty()) {
        return std::nullopt;
    }

    const size_t index = drag.guide.separatorIndex;
    if (index + 1 >= freeWeights.size()) {
        return std::nullopt;
    }

    const int combined = freeWeights[index] + freeWeights[index + 1];
    if (combined <= 1) {
        return std::nullopt;
    }

    for (size_t candidateIndex = 0; candidateIndex < drag.snapCandidates.size();) {
        const auto& widget = drag.snapCandidates[candidateIndex].widget;
        std::vector<layout_snap_solver::SnapCandidate> groupedCandidates;
        while (candidateIndex < drag.snapCandidates.size() &&
               MatchesWidgetIdentity(drag.snapCandidates[candidateIndex].widget, widget)) {
            const auto& candidate = drag.snapCandidates[candidateIndex];
            groupedCandidates.push_back(layout_snap_solver::SnapCandidate{
                candidate.targetExtent,
                candidate.startDistance,
                candidate.groupOrder,
            });
            ++candidateIndex;
        }

        const auto snappedWeight = layout_snap_solver::FindNearestSnapWeight(
            freeWeights[index], combined, threshold, groupedCandidates, [&](int firstWeight) -> std::optional<int> {
                std::vector<int> attemptWeights = freeWeights;
                attemptWeights[index] = firstWeight;
                attemptWeights[index + 1] = combined - firstWeight;
                const ExtentCacheKey cacheKey{attemptWeights, widget};
                if (const auto cached = drag.extentCache.find(cacheKey); cached != drag.extentCache.end()) {
                    return cached->second;
                }

                std::optional<int> extent = host_.EvaluateLayoutWidgetExtentForWeights(
                    LayoutEditHost::LayoutTarget::ForGuide(drag.guide), attemptWeights, widget, drag.guide.axis);
                drag.extentCache.emplace(std::move(cacheKey), extent);
                return extent;
            });
        if (!snappedWeight.has_value()) {
            continue;
        }

        std::vector<int> exact = freeWeights;
        exact[index] = *snappedWeight;
        exact[index + 1] = combined - *snappedWeight;
        return exact;
    }

    return std::nullopt;
}

bool LayoutEditController::UpdateLayoutDrag(RenderPoint clientPoint) {
    LayoutDragState& drag = *activeLayoutDrag_;
    const int currentCoordinate = drag.guide.axis == LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y;
    const int delta = currentCoordinate - drag.dragStartCoordinate;
    const size_t index = drag.guide.separatorIndex;
    if (index + 1 >= drag.initialWeights.size()) {
        return false;
    }

    const int combined = drag.initialWeights[index] + drag.initialWeights[index + 1];
    if (combined <= 1) {
        return false;
    }

    std::vector<int> weights = drag.initialWeights;
    weights[index] = std::clamp(drag.initialWeights[index] + delta, 1, combined - 1);
    weights[index + 1] = combined - weights[index];
    if ((GetKeyState(VK_MENU) & 0x8000) == 0) {
        const auto snapStart = std::chrono::steady_clock::now();
        if (const auto snappedWeights = FindSnappedLayoutGuideWeights(drag, weights); snappedWeights.has_value()) {
            weights = *snappedWeights;
        }
        host_.RecordLayoutEditTracePhase(
            LayoutEditHost::TracePhase::Snap, std::chrono::steady_clock::now() - snapStart);
    }
    if (!host_.ApplyLayoutGuideWeights(LayoutEditHost::LayoutTarget::ForGuide(drag.guide), weights)) {
        return false;
    }

    const auto& guides = host_.LayoutEditRenderer().LayoutEditGuides();
    const auto guideIt = std::find_if(guides.begin(), guides.end(), [&](const LayoutEditGuide& candidate) {
        return candidate.renderCardId == drag.guide.renderCardId && candidate.editCardId == drag.guide.editCardId &&
               candidate.nodePath == drag.guide.nodePath && candidate.separatorIndex == drag.guide.separatorIndex;
    });
    if (guideIt != guides.end()) {
        drag.guide = *guideIt;
    }

    SyncRendererInteractionState();
    RefreshHover(clientPoint);
    return true;
}

bool LayoutEditController::UpdateWidgetEditDrag(RenderPoint clientPoint) {
    WidgetEditDragState& drag = *activeWidgetEditDrag_;
    double nextValue = drag.initialValue;
    if (drag.guide.angularDrag) {
        if (drag.guide.parameter == DashboardRenderer::LayoutEditParameter::GaugeSweepDegrees) {
            const auto sweepDegrees = ComputeGaugeSweepDegrees(drag.guide.dragOrigin, clientPoint);
            if (!sweepDegrees.has_value()) {
                return true;
            }
            nextValue = *sweepDegrees;
        } else if (drag.guide.parameter == DashboardRenderer::LayoutEditParameter::GaugeSegmentGapDegrees) {
            const auto segmentGapDegrees = ComputeGaugeSegmentGapDegrees(drag.guide, clientPoint);
            if (!segmentGapDegrees.has_value()) {
                return true;
            }
            nextValue = ClampGaugeSegmentGapForCurrentConfig(host_.LayoutEditConfig(), *segmentGapDegrees);
        } else {
            return false;
        }
    } else {
        const int currentCoordinate = drag.guide.axis == LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y;
        const int pixelDelta = currentCoordinate - drag.dragStartCoordinate;
        const int logicalDelta =
            static_cast<int>(std::lround(static_cast<double>(pixelDelta * drag.guide.dragDirection) /
                                         (std::max)(0.1, host_.LayoutEditRenderer().RenderScale())));
        nextValue = (std::max)(0.0, drag.initialValue + static_cast<double>(logicalDelta));
        if (drag.guide.parameter == DashboardRenderer::LayoutEditParameter::DriveUsageActivitySegmentGap) {
            nextValue = ClampDriveUsageActivitySegmentGapForCurrentConfig(host_.LayoutEditConfig(), nextValue);
        }
    }
    if (!host_.ApplyLayoutEditValue(drag.guide.parameter, nextValue)) {
        return false;
    }

    const auto& guides = host_.LayoutEditRenderer().WidgetEditGuides();
    const auto guideIt = std::find_if(guides.begin(), guides.end(), [&](const LayoutEditWidgetGuide& candidate) {
        return candidate.parameter == drag.guide.parameter && candidate.guideId == drag.guide.guideId &&
               candidate.widget.kind == drag.guide.widget.kind &&
               candidate.widget.renderCardId == drag.guide.widget.renderCardId &&
               candidate.widget.editCardId == drag.guide.widget.editCardId &&
               candidate.widget.nodePath == drag.guide.widget.nodePath;
    });
    if (guideIt != guides.end()) {
        drag.guide = *guideIt;
    }

    SyncRendererInteractionState();
    return true;
}

bool LayoutEditController::UpdateGapEditDrag(RenderPoint clientPoint) {
    GapEditDragState& drag = *activeGapEditDrag_;
    const int currentCoordinate = drag.anchor.dragAxis == AnchorDragAxis::Horizontal ? clientPoint.x : clientPoint.y;
    const int pixelDelta = currentCoordinate - drag.dragStartCoordinate;
    const int logicalDelta = static_cast<int>(
        std::lround(static_cast<double>(pixelDelta) / (std::max)(0.1, host_.LayoutEditRenderer().RenderScale())));
    const double nextValue = (std::max)(0.0, drag.initialValue + static_cast<double>(logicalDelta));
    if (!host_.ApplyLayoutEditValue(drag.anchor.key.parameter, nextValue)) {
        return false;
    }

    const auto anchor = host_.LayoutEditRenderer().FindGapEditAnchor(drag.anchor.key);
    if (anchor.has_value()) {
        drag.anchor = *anchor;
    }

    SyncRendererInteractionState();
    return true;
}

bool LayoutEditController::UpdateAnchorEditDrag(RenderPoint clientPoint) {
    AnchorEditDragState& drag = *activeAnchorEditDrag_;
    const auto parameter = LayoutEditAnchorParameter(drag.key);
    if (!parameter.has_value()) {
        return false;
    }
    int logicalDelta = 0;
    if (drag.dragMode == AnchorDragMode::RadialDistance) {
        const double dx = static_cast<double>(clientPoint.x - drag.dragOrigin.x);
        const double dy = static_cast<double>(clientPoint.y - drag.dragOrigin.y);
        const double distanceDeltaPixels = std::sqrt((dx * dx) + (dy * dy)) - drag.dragStartDistancePixels;
        logicalDelta = static_cast<int>(std::lround(
            distanceDeltaPixels * drag.dragScale / (std::max)(0.1, host_.LayoutEditRenderer().RenderScale())));
    } else {
        int pixelDelta = 0;
        double scaleDivisor = drag.dragAxis == AnchorDragAxis::Vertical ? 4.0 : 1.0;
        if (drag.dragAxis == AnchorDragAxis::Both) {
            pixelDelta = (clientPoint.x - drag.dragStartPoint.x) + (clientPoint.y - drag.dragStartPoint.y);
            scaleDivisor = 4.0;
        } else {
            const int currentCoordinate = drag.dragAxis == AnchorDragAxis::Vertical ? clientPoint.x : clientPoint.y;
            const int startCoordinate =
                drag.dragAxis == AnchorDragAxis::Vertical ? drag.dragStartPoint.x : drag.dragStartPoint.y;
            pixelDelta = currentCoordinate - startCoordinate;
        }
        logicalDelta =
            static_cast<int>(std::lround(static_cast<double>(pixelDelta) /
                                         (std::max)(0.1, host_.LayoutEditRenderer().RenderScale() * scaleDivisor)));
    }
    const int nextValue = (std::max)(1, drag.initialValue + logicalDelta);
    const bool updated = host_.ApplyLayoutEditValue(*parameter, static_cast<double>(nextValue));
    if (updated) {
        SyncRendererInteractionState();
    }
    return updated;
}

bool LayoutEditController::UpdateMetricListReorderDrag(RenderPoint clientPoint) {
    MetricListReorderDragState& drag = *activeMetricListReorderDrag_;
    const int relativeY = clientPoint.y - drag.rowTop;
    const int targetIndex = std::clamp(relativeY / (std::max)(1, drag.rowHeight), 0, (std::max)(0, drag.rowCount - 1));
    if (targetIndex == drag.currentIndex) {
        return true;
    }

    std::vector<std::string> nextMetricRefs = drag.metricRefs;
    const std::string movedMetric = nextMetricRefs[drag.currentIndex];
    nextMetricRefs.erase(nextMetricRefs.begin() + drag.currentIndex);
    nextMetricRefs.insert(nextMetricRefs.begin() + targetIndex, movedMetric);
    if (!host_.ApplyMetricListOrder(drag.widget, nextMetricRefs)) {
        return false;
    }

    drag.metricRefs = std::move(nextMetricRefs);
    drag.currentIndex = targetIndex;
    SyncRendererInteractionState();
    RefreshHover(clientPoint);
    return true;
}
