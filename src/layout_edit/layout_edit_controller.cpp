#include "layout_edit/layout_edit_controller.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "layout_edit/impl/layout_snap_solver.h"
#include "layout_edit/layout_edit_hit_test.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_parameter_metadata.h"

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
                                : LayoutEditAnchorNodeFieldKey(key).has_value()           ? "node_field"
                                : LayoutEditAnchorContainerChildOrderKey(key).has_value() ? "container_child_reorder"
                                                                                          : "subject=unknown";
    return subject + " anchor_id=" + std::to_string(key.anchorId) +
           (key.widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome
                   ? " card=" + key.widget.editCardId
                   : " path=" + FormatNodePath(key.widget.nodePath));
}

int ContainerChildAxisStart(const RenderRect& rect, bool horizontal) {
    return horizontal ? rect.left : rect.top;
}

int ContainerChildAxisEnd(const RenderRect& rect, bool horizontal) {
    return horizontal ? rect.right : rect.bottom;
}

int ContainerChildAxisExtent(const RenderRect& rect, bool horizontal) {
    return (std::max)(1, ContainerChildAxisEnd(rect, horizontal) - ContainerChildAxisStart(rect, horizontal));
}

int ContainerChildAxisCenter(const RenderRect& rect, bool horizontal) {
    return ContainerChildAxisStart(rect, horizontal) + (ContainerChildAxisExtent(rect, horizontal) / 2);
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

LayoutEditHoverResolution LayoutEditHost::ResolveLayoutEditHover(RenderPoint clientPoint) const {
    return ::ResolveLayoutEditHover(CollectLayoutEditActiveRegions(), clientPoint);
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

LayoutEditActiveRegions LayoutEditController::ActiveRegions() const {
    return host_.CollectLayoutEditActiveRegions();
}

LayoutEditHoverResolution LayoutEditController::ResolveHover(RenderPoint clientPoint) const {
    return host_.ResolveLayoutEditHover(clientPoint);
}

void LayoutEditController::RefreshHover(RenderPoint clientPoint) {
    if (activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeGapEditDrag_.has_value() ||
        activeAnchorEditDrag_.has_value()) {
        return;
    }

    const LayoutEditHoverResolution resolution = ResolveHover(clientPoint);
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

    const std::optional<LayoutEditGapAnchor>& nextGapAnchorRegion = resolution.hoveredGapEditAnchorRegion;
    if (hoveredGapEditAnchorRegion_.has_value() != nextGapAnchorRegion.has_value() ||
        (hoveredGapEditAnchorRegion_.has_value() && nextGapAnchorRegion.has_value() &&
            !MatchesGapEditAnchorKey(hoveredGapEditAnchorRegion_->key, nextGapAnchorRegion->key))) {
        hoveredGapEditAnchorRegion_ = nextGapAnchorRegion;
        hoverChanged = true;
    }

    if (hoveredEditableAnchor_.has_value() != nextHoveredAnchor.has_value() ||
        (hoveredEditableAnchor_.has_value() && nextHoveredAnchor.has_value() &&
            !MatchesEditableAnchorKey(*hoveredEditableAnchor_, *nextHoveredAnchor))) {
        hoveredEditableAnchor_ = nextHoveredAnchor;
        host_.LayoutDashboardOverlayState().hoveredEditableAnchor = hoveredEditableAnchor_;
        hoverChanged = true;
    }

    const std::optional<LayoutEditWidgetGuide>& nextWidgetGuide = resolution.hoveredWidgetEditGuide;
    if (hoveredWidgetEditGuide_.has_value() != nextWidgetGuide.has_value() ||
        (hoveredWidgetEditGuide_.has_value() && nextWidgetGuide.has_value() &&
            (hoveredWidgetEditGuide_->parameter != nextWidgetGuide->parameter ||
                hoveredWidgetEditGuide_->guideId != nextWidgetGuide->guideId ||
                !MatchesWidgetIdentity(hoveredWidgetEditGuide_->widget, nextWidgetGuide->widget)))) {
        hoveredWidgetEditGuide_ = nextWidgetGuide;
        hoverChanged = true;
    }

    const std::optional<LayoutEditGuide>& nextLayoutGuide = resolution.hoveredLayoutGuide;
    if (hoveredLayoutGuide_.has_value() != nextLayoutGuide.has_value() ||
        (hoveredLayoutGuide_.has_value() && nextLayoutGuide.has_value() &&
            (hoveredLayoutGuide_->renderCardId != nextLayoutGuide->renderCardId ||
                hoveredLayoutGuide_->editCardId != nextLayoutGuide->editCardId ||
                hoveredLayoutGuide_->nodePath != nextLayoutGuide->nodePath ||
                hoveredLayoutGuide_->separatorIndex != nextLayoutGuide->separatorIndex))) {
        hoveredLayoutGuide_ = nextLayoutGuide;
        host_.LayoutDashboardOverlayState().hoveredLayoutEditGuide = hoveredLayoutGuide_;
        hoverChanged = true;
    }

    if (hoverChanged) {
        host_.InvalidateLayoutEdit();
    }

    SetCursorForPoint(clientPoint);
}

bool LayoutEditController::HandleLButtonDown(HWND hwnd, RenderPoint clientPoint) {
    lastClientPoint_ = clientPoint;
    const LayoutEditActiveRegions regions = ActiveRegions();
    const LayoutEditHoverResolution resolution = ResolveLayoutEditHover(regions, clientPoint);
    hoveredLayoutCard_ = resolution.hoveredLayoutCard;
    hoveredEditableCard_ = resolution.hoveredEditableCard;
    hoveredEditableWidget_ = resolution.hoveredEditableWidget;
    hoveredGapEditAnchor_ = resolution.hoveredGapEditAnchor;
    hoveredGapEditAnchorRegion_ = resolution.hoveredGapEditAnchorRegion;
    hoveredWidgetEditGuide_ = resolution.hoveredWidgetEditGuide;
    hoveredLayoutGuide_ = resolution.hoveredLayoutGuide;
    hoveredEditableAnchor_ = resolution.hoveredEditableAnchor;
    if (resolution.actionableAnchorHandle.has_value()) {
        const auto region = FindEditableAnchorRegion(regions, *resolution.actionableAnchorHandle);
        if (region.has_value()) {
            if (const auto containerOrderKey = LayoutEditAnchorContainerChildOrderKey(region->key);
                containerOrderKey.has_value()) {
                const LayoutNodeConfig* node = FindGuideNode(host_.LayoutEditConfig(),
                    LayoutEditLayoutTarget{containerOrderKey->editCardId, containerOrderKey->nodePath});
                if (node == nullptr || (node->name != "rows" && node->name != "columns") || region->key.anchorId < 0 ||
                    region->key.anchorId >= static_cast<int>(node->children.size())) {
                    return false;
                }
                ContainerChildReorderDragState drag;
                drag.widget = region->key.widget;
                drag.key = *containerOrderKey;
                drag.horizontal = node->name == "columns";
                drag.currentIndex = region->key.anchorId;
                drag.originalIndex = region->key.anchorId;
                drag.childCount = static_cast<int>(node->children.size());
                drag.containerStart = drag.horizontal ? region->targetRect.left : region->targetRect.top;
                drag.draggedExtent = ContainerChildAxisExtent(region->targetRect, drag.horizontal);
                drag.dragOffset = (drag.horizontal ? clientPoint.x : clientPoint.y) -
                                  (drag.horizontal ? region->targetRect.left : region->targetRect.top);
                drag.mouseCoordinate = drag.horizontal ? clientPoint.x : clientPoint.y;
                activeContainerChildReorderDrag_ = std::move(drag);
                RefreshContainerChildReorderRects(*activeContainerChildReorderDrag_);
                ContainerChildReorderDragState& activeDrag = *activeContainerChildReorderDrag_;
                activeDrag.stableSnapCenters.clear();
                activeDrag.stableSnapCenters.reserve(activeDrag.childRects.size());
                for (const RenderRect& childRect : activeDrag.childRects) {
                    activeDrag.stableSnapCenters.push_back(ContainerChildAxisCenter(childRect, activeDrag.horizontal));
                }
                if (activeDrag.currentIndex >= 0 &&
                    activeDrag.currentIndex < static_cast<int>(activeDrag.childRects.size())) {
                    activeDrag.draggedExtent = ContainerChildAxisExtent(
                        activeDrag.childRects[static_cast<size_t>(activeDrag.currentIndex)], activeDrag.horizontal);
                }
                host_.SetLayoutEditInteractiveDragTraceActive(true);
                host_.BeginLayoutEditTraceSession("container_child_reorder", DescribeEditableAnchor(region->key));
                SyncRendererInteractionState();
                host_.InvalidateLayoutEdit();
                SetCapture(hwnd);
                return true;
            }
            if (const auto nodeFieldKey = LayoutEditAnchorNodeFieldKey(region->key);
                nodeFieldKey.has_value() && nodeFieldKey->widgetClass == WidgetClass::MetricList) {
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
                    region->key.anchorId,
                    clientPoint.y - static_cast<int>(region->targetRect.top),
                    clientPoint.y};
                hoveredEditableWidget_ = region->key.widget;
                host_.SetLayoutEditInteractiveDragTraceActive(true);
                host_.BeginLayoutEditTraceSession("metric_list_reorder", DescribeEditableAnchor(region->key));
                SyncRendererInteractionState();
                host_.InvalidateLayoutEdit();
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
            host_.SetLayoutEditInteractiveDragTraceActive(true);
            host_.BeginLayoutEditTraceSession("anchor", DescribeEditableAnchor(region->key));
            SyncRendererInteractionState();
            SetCapture(hwnd);
            return true;
        }
    }

    if (const auto anchorHandle = HitTestEditableAnchorHandle(regions, clientPoint);
        anchorHandle.has_value() && !anchorHandle->draggable) {
        hoveredEditableAnchor_ = anchorHandle->key;
        if (anchorHandle->key.widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            hoveredEditableCard_ = anchorHandle->key.widget;
            hoveredEditableWidget_.reset();
        } else {
            hoveredEditableWidget_ = anchorHandle->key.widget;
        }
        SyncRendererInteractionState();
        host_.InvalidateLayoutEdit();
        return true;
    }

    if (resolution.actionableGapEditAnchor.has_value()) {
        const auto anchor = FindGapEditAnchor(regions, *resolution.actionableGapEditAnchor);
        if (anchor.has_value()) {
            activeGapEditDrag_ = GapEditDragState{
                *anchor,
                anchor->value,
                anchor->dragAxis == AnchorDragAxis::Horizontal ? clientPoint.x : clientPoint.y,
            };
            hoveredGapEditAnchorRegion_ = anchor;
            host_.SetLayoutEditInteractiveDragTraceActive(true);
            host_.BeginLayoutEditTraceSession("gap_anchor", DescribeGapEditAnchor(*anchor));
            SyncRendererInteractionState();
            SetCapture(hwnd);
            return true;
        }
    }

    if (resolution.hoveredWidgetEditGuide.has_value()) {
        const LayoutEditWidgetGuide& widgetGuide = *resolution.hoveredWidgetEditGuide;
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
        hoveredWidgetEditGuide_ = widgetGuide;
        host_.SetLayoutEditInteractiveDragTraceActive(true);
        host_.BeginLayoutEditTraceSession("widget_guide", DescribeWidgetGuide(widgetGuide));
        SyncRendererInteractionState();
        SetCapture(hwnd);
        return true;
    }

    if (resolution.hoveredLayoutGuide.has_value()) {
        const LayoutEditGuide& guide = *resolution.hoveredLayoutGuide;
        const LayoutNodeConfig* guideNode =
            FindGuideNode(host_.LayoutEditConfig(), LayoutEditLayoutTarget::ForGuide(guide));
        const std::vector<int> initialWeights = SeedGuideWeights(guide, guideNode);
        activeLayoutDrag_ = LayoutDragState{
            guide,
            initialWeights,
            CollectLayoutGuideSnapCandidates(regions, guide),
            guide.axis == LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y,
        };
        host_.SetLayoutGuideDragActive(true);
        host_.SetLayoutEditInteractiveDragTraceActive(true);
        hoveredLayoutGuide_ = guide;
        host_.BeginLayoutEditTraceSession("layout_guide", DescribeLayoutGuide(guide));
        SyncRendererInteractionState();
        SetCapture(hwnd);
        return true;
    }

    return false;
}

bool LayoutEditController::HandleMouseMove(RenderPoint clientPoint) {
    lastClientPoint_ = clientPoint;
    if (activeLayoutDrag_.has_value()) {
        return UpdateLayoutDrag(clientPoint);
    }
    if (activeMetricListReorderDrag_.has_value()) {
        return UpdateMetricListReorderDrag(clientPoint);
    }
    if (activeContainerChildReorderDrag_.has_value()) {
        return UpdateContainerChildReorderDrag(clientPoint);
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

    host_.LayoutDashboardOverlayState().hoverOnExposedDashboard = true;
    RefreshHover(clientPoint);
    return true;
}

bool LayoutEditController::HandleMouseLeave() {
    if (activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeGapEditDrag_.has_value() ||
        activeAnchorEditDrag_.has_value() || activeMetricListReorderDrag_.has_value() ||
        activeContainerChildReorderDrag_.has_value()) {
        return false;
    }

    host_.LayoutDashboardOverlayState().hoverOnExposedDashboard = false;
    lastClientPoint_.reset();

    const bool hadHover = hoveredLayoutGuide_.has_value() || hoveredLayoutCard_.has_value() ||
                          hoveredEditableCard_.has_value() || hoveredEditableWidget_.has_value() ||
                          hoveredGapEditAnchorRegion_.has_value() || hoveredGapEditAnchor_.has_value() ||
                          hoveredWidgetEditGuide_.has_value() || hoveredEditableAnchor_.has_value();
    if (!hadHover) {
        return false;
    }

    hoveredLayoutGuide_.reset();
    hoveredLayoutCard_.reset();
    hoveredEditableCard_.reset();
    hoveredEditableWidget_.reset();
    hoveredGapEditAnchorRegion_.reset();
    hoveredGapEditAnchor_.reset();
    hoveredWidgetEditGuide_.reset();
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
    } else if (activeContainerChildReorderDrag_.has_value()) {
        activeContainerChildReorderDrag_.reset();
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
        host_.SetLayoutGuideDragActive(false);
        host_.RebuildLayoutEditArtifacts();
    }
    host_.SetLayoutEditInteractiveDragTraceActive(false);
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
                               activeContainerChildReorderDrag_.has_value() || activeGapEditDrag_.has_value() ||
                               activeWidgetEditDrag_.has_value() || activeLayoutDrag_.has_value();
    if (!hadActiveDrag) {
        return false;
    }

    activeAnchorEditDrag_.reset();
    activeMetricListReorderDrag_.reset();
    activeContainerChildReorderDrag_.reset();
    activeGapEditDrag_.reset();
    activeWidgetEditDrag_.reset();
    const bool hadLayoutDrag = activeLayoutDrag_.has_value();
    activeLayoutDrag_.reset();
    if (hadLayoutDrag) {
        host_.SetLayoutGuideDragActive(false);
        host_.RebuildLayoutEditArtifacts();
    }
    host_.SetLayoutEditInteractiveDragTraceActive(false);
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
    if (activeContainerChildReorderDrag_.has_value()) {
        SetCursor(LoadCursorW(nullptr, activeContainerChildReorderDrag_->horizontal ? IDC_SIZEWE : IDC_SIZENS));
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
           activeAnchorEditDrag_.has_value() || activeMetricListReorderDrag_.has_value() ||
           activeContainerChildReorderDrag_.has_value();
}

void LayoutEditController::CancelInteraction() {
    const bool hadInteraction =
        hoveredLayoutGuide_.has_value() || hoveredLayoutCard_.has_value() || hoveredEditableCard_.has_value() ||
        hoveredEditableWidget_.has_value() || hoveredGapEditAnchorRegion_.has_value() ||
        hoveredGapEditAnchor_.has_value() || hoveredWidgetEditGuide_.has_value() ||
        hoveredEditableAnchor_.has_value() || activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() ||
        activeGapEditDrag_.has_value() || activeAnchorEditDrag_.has_value() ||
        activeMetricListReorderDrag_.has_value() || activeContainerChildReorderDrag_.has_value();
    const bool hadLayoutDrag = activeLayoutDrag_.has_value();
    if (!hadInteraction) {
        return;
    }

    ClearInteractionState();
    if (hadLayoutDrag) {
        host_.RebuildLayoutEditArtifacts();
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

    const LayoutEditActiveRegions regions = ActiveRegions();
    if (activeMetricListReorderDrag_.has_value()) {
        const LayoutEditAnchorKey key{activeMetricListReorderDrag_->widget,
            LayoutNodeFieldEditKey{activeMetricListReorderDrag_->widget.editCardId,
                activeMetricListReorderDrag_->widget.nodePath,
                WidgetClass::MetricList,
                LayoutNodeField::Parameter},
            activeMetricListReorderDrag_->currentIndex};
        const auto region = FindEditableAnchorRegion(regions, key);
        if (region.has_value()) {
            return TooltipTarget{clientPoint, *region};
        }
    }
    if (activeContainerChildReorderDrag_.has_value()) {
        for (int anchorId : {activeContainerChildReorderDrag_->currentIndex, 0}) {
            const LayoutEditAnchorKey key{
                activeContainerChildReorderDrag_->widget, activeContainerChildReorderDrag_->key, anchorId};
            const auto region = FindEditableAnchorRegion(regions, key);
            if (region.has_value()) {
                return TooltipTarget{clientPoint, *region};
            }
        }
    }
    if (activeGapEditDrag_.has_value()) {
        const auto anchor = FindGapEditAnchor(regions, activeGapEditDrag_->anchor.key);
        if (anchor.has_value()) {
            return TooltipTarget{clientPoint, *anchor};
        }
    }

    if (activeWidgetEditDrag_.has_value()) {
        return TooltipTarget{clientPoint, activeWidgetEditDrag_->guide};
    }

    if (activeAnchorEditDrag_.has_value()) {
        const auto region = FindEditableAnchorRegion(regions, activeAnchorEditDrag_->key);
        if (region.has_value()) {
            return TooltipTarget{clientPoint, *region};
        }
    }

    const LayoutEditHoverResolution resolution = ResolveLayoutEditHover(regions, clientPoint);
    if (resolution.actionableGapEditAnchor.has_value()) {
        const auto anchor = FindGapEditAnchor(regions, *resolution.actionableGapEditAnchor);
        if (anchor.has_value()) {
            return TooltipTarget{clientPoint, *anchor};
        }
    }

    if (resolution.actionableAnchorHandle.has_value()) {
        const auto region = FindEditableAnchorRegion(regions, *resolution.actionableAnchorHandle);
        if (region.has_value()) {
            return TooltipTarget{clientPoint, *region};
        }
    }

    if (const auto anchorHandle = HitTestEditableAnchorHandle(regions, clientPoint);
        anchorHandle.has_value() && !anchorHandle->draggable) {
        return TooltipTarget{clientPoint, *anchorHandle};
    }

    if (resolution.hoveredWidgetEditGuide.has_value()) {
        return TooltipTarget{clientPoint, *resolution.hoveredWidgetEditGuide};
    }

    if (resolution.hoveredLayoutGuide.has_value()) {
        return TooltipTarget{clientPoint, *resolution.hoveredLayoutGuide};
    }

    if (const auto colorRegion = HitTestEditableColorRegion(regions, clientPoint); colorRegion.has_value()) {
        return TooltipTarget{clientPoint, *colorRegion};
    }

    return std::nullopt;
}

void LayoutEditController::SyncRendererInteractionState() {
    DashboardOverlayState& overlayState = host_.LayoutDashboardOverlayState();
    overlayState.hoveredLayoutEditGuide = hoveredLayoutGuide_;
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
            LayoutNodeFieldEditKey{activeMetricListReorderDrag_->widget.editCardId,
                activeMetricListReorderDrag_->widget.nodePath,
                WidgetClass::MetricList,
                LayoutNodeField::Parameter},
            activeMetricListReorderDrag_->currentIndex};
        overlayState.activeMetricListReorderDrag = MetricListReorderOverlayState{activeMetricListReorderDrag_->widget,
            activeMetricListReorderDrag_->currentIndex,
            activeMetricListReorderDrag_->mouseY,
            activeMetricListReorderDrag_->dragOffsetY};
    } else {
        overlayState.activeMetricListReorderDrag.reset();
    }
    if (activeContainerChildReorderDrag_.has_value()) {
        overlayState.activeEditableAnchor = LayoutEditAnchorKey{activeContainerChildReorderDrag_->widget,
            activeContainerChildReorderDrag_->key,
            activeContainerChildReorderDrag_->currentIndex};
        overlayState.activeContainerChildReorderDrag =
            ContainerChildReorderOverlayState{activeContainerChildReorderDrag_->key,
                activeContainerChildReorderDrag_->childRects,
                activeContainerChildReorderDrag_->currentIndex,
                activeContainerChildReorderDrag_->mouseCoordinate,
                activeContainerChildReorderDrag_->dragOffset,
                activeContainerChildReorderDrag_->horizontal};
    } else {
        overlayState.activeContainerChildReorderDrag.reset();
    }
    if (HasActiveDrag()) {
        overlayState.hoverOnExposedDashboard = false;
        overlayState.hoveredLayoutEditGuide.reset();
        overlayState.hoveredLayoutCard.reset();
        overlayState.hoveredGapEditAnchor.reset();
        overlayState.hoveredEditableAnchor.reset();
    }
}

void LayoutEditController::ClearInteractionState() {
    lastClientPoint_.reset();
    hoveredLayoutGuide_.reset();
    hoveredLayoutCard_.reset();
    hoveredEditableCard_.reset();
    hoveredEditableWidget_.reset();
    hoveredGapEditAnchorRegion_.reset();
    hoveredGapEditAnchor_.reset();
    hoveredWidgetEditGuide_.reset();
    hoveredEditableAnchor_.reset();
    host_.SetLayoutGuideDragActive(false);
    host_.SetLayoutEditInteractiveDragTraceActive(false);
    activeLayoutDrag_.reset();
    activeWidgetEditDrag_.reset();
    activeGapEditDrag_.reset();
    activeAnchorEditDrag_.reset();
    activeMetricListReorderDrag_.reset();
    activeContainerChildReorderDrag_.reset();
    host_.LayoutDashboardOverlayState().hoverOnExposedDashboard = false;
}

void LayoutEditController::SetCursorForPoint(RenderPoint clientPoint) {
    const LayoutEditActiveRegions regions = ActiveRegions();
    const LayoutEditHoverResolution resolution = ResolveLayoutEditHover(regions, clientPoint);
    if (resolution.actionableGapEditAnchor.has_value()) {
        const auto anchor = FindGapEditAnchor(regions, *resolution.actionableGapEditAnchor);
        const auto dragAxis = anchor.has_value() ? anchor->dragAxis : AnchorDragAxis::Vertical;
        SetCursor(LoadCursorW(nullptr, dragAxis == AnchorDragAxis::Horizontal ? IDC_SIZEWE : IDC_SIZENS));
        return;
    }

    if (resolution.actionableAnchorHandle.has_value()) {
        const auto region = FindEditableAnchorRegion(regions, *resolution.actionableAnchorHandle);
        const auto dragAxis = region.has_value() ? region->dragAxis : AnchorDragAxis::Vertical;
        const auto dragMode = region.has_value() ? region->dragMode : AnchorDragMode::AxisDelta;
        SetCursor(LoadCursorW(nullptr,
            dragMode == AnchorDragMode::RadialDistance ? IDC_SIZEALL
            : dragAxis == AnchorDragAxis::Both         ? IDC_SIZEALL
            : dragAxis == AnchorDragAxis::Vertical     ? IDC_SIZEWE
                                                       : IDC_SIZENS));
        return;
    }

    if (resolution.hoveredWidgetEditGuide.has_value()) {
        const LayoutEditWidgetGuide& widgetGuide = *resolution.hoveredWidgetEditGuide;
        SetCursor(LoadCursorW(nullptr,
            widgetGuide.angularDrag                         ? IDC_CROSS
            : widgetGuide.axis == LayoutGuideAxis::Vertical ? IDC_SIZEWE
                                                            : IDC_SIZENS));
        return;
    }

    if (resolution.hoveredLayoutGuide.has_value()) {
        const LayoutEditGuide& layoutGuide = *resolution.hoveredLayoutGuide;
        SetCursor(LoadCursorW(nullptr, layoutGuide.axis == LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
        return;
    }

    if (HitTestEditableColorRegion(regions, clientPoint).has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        return;
    }

    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

std::optional<std::vector<int>> LayoutEditController::FindSnappedLayoutGuideWeights(
    LayoutDragState& drag, const std::vector<int>& freeWeights) {
    const int threshold = host_.LayoutEditSimilarityThreshold();
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
                    LayoutEditLayoutTarget::ForGuide(drag.guide), attemptWeights, widget, drag.guide.axis);
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
    if (!host_.ApplyLayoutGuideWeights(LayoutEditLayoutTarget::ForGuide(drag.guide), weights)) {
        return false;
    }

    if (const auto guide = FindLayoutEditGuide(ActiveRegions(), drag.guide); guide.has_value()) {
        drag.guide = *guide;
    }

    SyncRendererInteractionState();
    return true;
}

bool LayoutEditController::UpdateWidgetEditDrag(RenderPoint clientPoint) {
    WidgetEditDragState& drag = *activeWidgetEditDrag_;
    double nextValue = drag.initialValue;
    if (drag.guide.angularDrag) {
        if (drag.guide.parameter == LayoutEditParameter::GaugeSweepDegrees) {
            const auto sweepDegrees = ComputeGaugeSweepDegrees(drag.guide.dragOrigin, clientPoint);
            if (!sweepDegrees.has_value()) {
                return true;
            }
            nextValue = *sweepDegrees;
        } else if (drag.guide.parameter == LayoutEditParameter::GaugeSegmentGapDegrees) {
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
                                         (std::max)(0.1, host_.LayoutEditRenderScale())));
        nextValue = (std::max)(0.0, drag.initialValue + static_cast<double>(logicalDelta));
        if (drag.guide.parameter == LayoutEditParameter::DriveUsageActivitySegmentGap) {
            nextValue = ClampDriveUsageActivitySegmentGapForCurrentConfig(host_.LayoutEditConfig(), nextValue);
        }
    }
    if (!host_.ApplyLayoutEditValue(drag.guide.parameter, nextValue)) {
        return false;
    }

    if (const auto guide = FindWidgetEditGuide(ActiveRegions(), drag.guide); guide.has_value()) {
        drag.guide = *guide;
    }

    SyncRendererInteractionState();
    return true;
}

bool LayoutEditController::UpdateGapEditDrag(RenderPoint clientPoint) {
    GapEditDragState& drag = *activeGapEditDrag_;
    const int currentCoordinate = drag.anchor.dragAxis == AnchorDragAxis::Horizontal ? clientPoint.x : clientPoint.y;
    const int pixelDelta = currentCoordinate - drag.dragStartCoordinate;
    const int logicalDelta =
        static_cast<int>(std::lround(static_cast<double>(pixelDelta) / (std::max)(0.1, host_.LayoutEditRenderScale())));
    const double nextValue = (std::max)(0.0, drag.initialValue + static_cast<double>(logicalDelta));
    if (!host_.ApplyLayoutEditValue(drag.anchor.key.parameter, nextValue)) {
        return false;
    }

    const auto anchor = FindGapEditAnchor(ActiveRegions(), drag.anchor.key);
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
        logicalDelta = static_cast<int>(
            std::lround(distanceDeltaPixels * drag.dragScale / (std::max)(0.1, host_.LayoutEditRenderScale())));
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
        logicalDelta = static_cast<int>(std::lround(
            static_cast<double>(pixelDelta) / (std::max)(0.1, host_.LayoutEditRenderScale() * scaleDivisor)));
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
    drag.mouseY = clientPoint.y;
    const int relativeY = clientPoint.y - drag.rowTop;
    const int targetIndex = std::clamp(relativeY / (std::max)(1, drag.rowHeight), 0, (std::max)(0, drag.rowCount - 1));
    if (targetIndex == drag.currentIndex) {
        SyncRendererInteractionState();
        host_.InvalidateLayoutEdit();
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
    host_.InvalidateLayoutEdit();
    return true;
}

void LayoutEditController::RefreshContainerChildReorderRects(ContainerChildReorderDragState& drag) {
    drag.childRects.clear();
    drag.childRects.resize(static_cast<size_t>((std::max)(0, drag.childCount)));
    for (int index = 0; index < drag.childCount; ++index) {
        const LayoutEditAnchorKey key{drag.widget, drag.key, index};
        if (const auto region = FindEditableAnchorRegion(ActiveRegions(), key); region.has_value()) {
            drag.childRects[static_cast<size_t>(index)] = region->targetRect;
        }
    }
    if (drag.currentIndex >= 0 && drag.currentIndex < static_cast<int>(drag.childRects.size())) {
        const RenderRect& currentRect = drag.childRects[static_cast<size_t>(drag.currentIndex)];
        drag.containerStart = drag.horizontal ? currentRect.left : currentRect.top;
    }
}

bool LayoutEditController::UpdateContainerChildReorderDrag(RenderPoint clientPoint) {
    ContainerChildReorderDragState& drag = *activeContainerChildReorderDrag_;
    drag.mouseCoordinate = drag.horizontal ? clientPoint.x : clientPoint.y;
    if (drag.stableSnapCenters.size() != static_cast<size_t>((std::max)(0, drag.childCount))) {
        SyncRendererInteractionState();
        host_.InvalidateLayoutEdit();
        return true;
    }

    const int draggedCenter = drag.mouseCoordinate - drag.dragOffset + (drag.draggedExtent / 2);
    int targetIndex = 0;
    for (int index = 0; index < static_cast<int>(drag.stableSnapCenters.size()); ++index) {
        if (index == drag.originalIndex) {
            continue;
        }
        if (draggedCenter > drag.stableSnapCenters[static_cast<size_t>(index)]) {
            ++targetIndex;
        }
    }
    targetIndex = std::clamp(targetIndex, 0, (std::max)(0, drag.childCount - 1));
    if (targetIndex == drag.currentIndex) {
        SyncRendererInteractionState();
        host_.InvalidateLayoutEdit();
        return true;
    }

    if (!host_.ApplyContainerChildOrder(drag.key, drag.currentIndex, targetIndex)) {
        return false;
    }

    drag.currentIndex = targetIndex;
    RefreshContainerChildReorderRects(drag);
    SyncRendererInteractionState();
    host_.InvalidateLayoutEdit();
    return true;
}
