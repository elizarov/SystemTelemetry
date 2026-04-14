#include "layout_edit_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "layout_edit_parameter.h"
#include "layout_edit_service.h"
#include "layout_snap_solver.h"

namespace {

bool WidgetIdentityEquals(
    const DashboardRenderer::LayoutWidgetIdentity& left, const DashboardRenderer::LayoutWidgetIdentity& right) {
    return left.renderCardId == right.renderCardId && left.editCardId == right.editCardId &&
           left.nodePath == right.nodePath;
}

bool EditableAnchorKeyEquals(
    const DashboardRenderer::EditableAnchorKey& left, const DashboardRenderer::EditableAnchorKey& right) {
    return WidgetIdentityEquals(left.widget, right.widget) && left.parameter == right.parameter &&
           left.anchorId == right.anchorId;
}

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

const char* AxisName(DashboardRenderer::LayoutGuideAxis axis) {
    return axis == DashboardRenderer::LayoutGuideAxis::Vertical ? "vertical" : "horizontal";
}

std::string DescribeLayoutGuide(const DashboardRenderer::LayoutEditGuide& guide) {
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

std::string DescribeWidgetGuide(const DashboardRenderer::WidgetEditGuide& guide) {
    return "axis=" + std::string(AxisName(guide.axis)) + " parameter=" + DescribeWidgetParameter(guide.parameter) +
           " guide_id=" + std::to_string(guide.guideId) + " path=" + FormatNodePath(guide.widget.nodePath);
}

std::string DescribeEditableAnchor(const DashboardRenderer::EditableAnchorKey& key) {
    return "parameter=" + DescribeWidgetParameter(key.parameter) + " anchor_id=" + std::to_string(key.anchorId) +
           " path=" + FormatNodePath(key.widget.nodePath);
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

std::optional<double> ComputeGaugeSegmentGapDegrees(
    const DashboardRenderer::WidgetEditGuide& guide, RenderPoint clientPoint) {
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

LayoutEditHost::LayoutTarget LayoutEditHost::LayoutTarget::ForGuide(const DashboardRenderer::LayoutEditGuide& guide) {
    LayoutTarget target;
    target.editCardId = guide.editCardId;
    target.nodePath = guide.nodePath;
    return target;
}

LayoutEditController::LayoutEditController(LayoutEditHost& host) : host_(host) {}

void LayoutEditController::StartSession() {
    host_.LayoutEditOverlayState().showLayoutEditGuides = true;
    ClearInteractionState();
    SyncRendererInteractionState();
}

void LayoutEditController::StopSession(bool showLayoutEditGuidesAfterStop) {
    ClearInteractionState();
    host_.LayoutEditOverlayState().showLayoutEditGuides = showLayoutEditGuidesAfterStop;
    SyncRendererInteractionState();
    ReleaseCapture();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    host_.EndLayoutEditTraceSession("session_stop");
    host_.InvalidateLayoutEdit();
}

const DashboardRenderer::LayoutEditGuide* LayoutEditController::HitTestLayoutGuide(
    RenderPoint clientPoint, size_t* index) const {
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

const DashboardRenderer::WidgetEditGuide* LayoutEditController::HitTestWidgetEditGuide(
    RenderPoint clientPoint, size_t* index) const {
    const auto& guides = host_.LayoutEditRenderer().WidgetEditGuides();
    const DashboardRenderer::WidgetEditGuide* bestGuide = nullptr;
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

LayoutEditController::HoverResolution LayoutEditController::ResolveHover(RenderPoint clientPoint) const {
    HoverResolution resolution;
    DashboardRenderer& renderer = host_.LayoutEditRenderer();

    const std::optional<DashboardRenderer::EditableAnchorKey> anchorHandle =
        renderer.HitTestEditableAnchorHandle(clientPoint);
    size_t widgetGuideIndex = 0;
    const DashboardRenderer::WidgetEditGuide* widgetGuide = HitTestWidgetEditGuide(clientPoint, &widgetGuideIndex);
    if (anchorHandle.has_value() && widgetGuide != nullptr) {
        const int anchorPriority = GetLayoutEditParameterHitPriority(anchorHandle->parameter);
        const int guidePriority = GetLayoutEditParameterHitPriority(widgetGuide->parameter);
        if (anchorPriority <= guidePriority) {
            resolution.hoveredEditableAnchor = anchorHandle;
            resolution.actionableAnchorHandle = anchorHandle;
            resolution.hoveredEditableWidget = anchorHandle->widget;
            return resolution;
        }

        resolution.hoveredEditableWidget = widgetGuide->widget;
        resolution.hoveredWidgetEditGuideIndex = widgetGuideIndex;
        return resolution;
    }

    if (anchorHandle.has_value()) {
        resolution.hoveredEditableAnchor = anchorHandle;
        resolution.actionableAnchorHandle = anchorHandle;
        resolution.hoveredEditableWidget = anchorHandle->widget;
        return resolution;
    }

    if (widgetGuide != nullptr) {
        resolution.hoveredEditableWidget = widgetGuide->widget;
        resolution.hoveredWidgetEditGuideIndex = widgetGuideIndex;
        return resolution;
    }

    const std::optional<DashboardRenderer::EditableAnchorKey> anchorTarget =
        renderer.HitTestEditableAnchorTarget(clientPoint);
    const std::optional<DashboardRenderer::LayoutWidgetIdentity> hoveredWidget =
        renderer.HitTestEditableWidget(clientPoint);
    if (hoveredWidget.has_value()) {
        resolution.hoveredEditableWidget = hoveredWidget;

        if (anchorTarget.has_value() && WidgetIdentityEquals(anchorTarget->widget, *hoveredWidget)) {
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
    if (activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeAnchorEditDrag_.has_value()) {
        return;
    }

    const HoverResolution resolution = ResolveHover(clientPoint);
    const std::optional<DashboardRenderer::LayoutWidgetIdentity>& nextHoveredWidget = resolution.hoveredEditableWidget;
    const std::optional<DashboardRenderer::EditableAnchorKey>& nextHoveredAnchor = resolution.hoveredEditableAnchor;

    bool hoverChanged = (hoveredEditableWidget_.has_value() != nextHoveredWidget.has_value());
    if (!hoverChanged && hoveredEditableWidget_.has_value() && nextHoveredWidget.has_value()) {
        hoverChanged = !WidgetIdentityEquals(*hoveredEditableWidget_, *nextHoveredWidget);
    }
    if (hoverChanged) {
        hoveredEditableWidget_ = nextHoveredWidget;
        host_.LayoutEditOverlayState().hoveredEditableWidget = hoveredEditableWidget_;
    }
    if (hoveredEditableAnchor_.has_value() != nextHoveredAnchor.has_value() ||
        (hoveredEditableAnchor_.has_value() && nextHoveredAnchor.has_value() &&
            !EditableAnchorKeyEquals(*hoveredEditableAnchor_, *nextHoveredAnchor))) {
        hoveredEditableAnchor_ = nextHoveredAnchor;
        host_.LayoutEditOverlayState().hoveredEditableAnchor = hoveredEditableAnchor_;
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
    if (resolution.actionableAnchorHandle.has_value()) {
        const auto region = renderer.FindEditableAnchorRegion(*resolution.actionableAnchorHandle);
        if (region.has_value()) {
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
            hoveredEditableWidget_ = region->key.widget;
            renderer.SetInteractiveDragTraceActive(true);
            host_.BeginLayoutEditTraceSession("anchor", DescribeEditableAnchor(region->key));
            SyncRendererInteractionState();
            SetCapture(hwnd);
            return true;
        }
    }

    if (resolution.hoveredWidgetEditGuideIndex.has_value()) {
        const auto& guides = renderer.WidgetEditGuides();
        const DashboardRenderer::WidgetEditGuide& widgetGuide = guides[*resolution.hoveredWidgetEditGuideIndex];
        activeWidgetEditDrag_ = WidgetEditDragState{
            widgetGuide,
            widgetGuide.value,
            widgetGuide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y,
        };
        hoveredEditableWidget_ = widgetGuide.widget;
        hoveredWidgetEditGuideIndex_ = resolution.hoveredWidgetEditGuideIndex;
        renderer.SetInteractiveDragTraceActive(true);
        host_.BeginLayoutEditTraceSession("widget_guide", DescribeWidgetGuide(widgetGuide));
        SyncRendererInteractionState();
        SetCapture(hwnd);
        return true;
    }

    if (resolution.hoveredLayoutGuideIndex.has_value()) {
        const auto& guides = renderer.LayoutEditGuides();
        const DashboardRenderer::LayoutEditGuide& guide = guides[*resolution.hoveredLayoutGuideIndex];
        const LayoutNodeConfig* guideNode =
            layout_edit::FindGuideNode(host_.LayoutEditConfig(), LayoutEditHost::LayoutTarget::ForGuide(guide));
        const std::vector<int> initialWeights = layout_edit::SeedGuideWeights(guide, guideNode);
        activeLayoutDrag_ = LayoutDragState{
            guide,
            initialWeights,
            renderer.CollectLayoutGuideSnapCandidates(guide),
            guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y,
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
    if (activeLayoutDrag_.has_value()) {
        return UpdateLayoutDrag(clientPoint);
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
    if (activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeAnchorEditDrag_.has_value()) {
        return false;
    }

    const bool hadHover = hoveredLayoutGuideIndex_.has_value() || hoveredEditableWidget_.has_value() ||
                          hoveredWidgetEditGuideIndex_.has_value() || hoveredEditableAnchor_.has_value();
    if (!hadHover) {
        return false;
    }

    hoveredLayoutGuideIndex_.reset();
    hoveredEditableWidget_.reset();
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

    const bool hadActiveDrag =
        activeAnchorEditDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeLayoutDrag_.has_value();
    if (!hadActiveDrag) {
        return false;
    }

    activeAnchorEditDrag_.reset();
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
            activeAnchorEditDrag_->dragMode == DashboardRenderer::AnchorDragMode::RadialDistance ? IDC_SIZEALL
            : activeAnchorEditDrag_->dragAxis == DashboardRenderer::AnchorDragAxis::Both         ? IDC_SIZEALL
            : activeAnchorEditDrag_->dragAxis == DashboardRenderer::AnchorDragAxis::Vertical     ? IDC_SIZEWE
                                                                                                 : IDC_SIZENS));
        return true;
    }
    if (activeWidgetEditDrag_.has_value()) {
        const auto& guide = activeWidgetEditDrag_->guide;
        SetCursor(LoadCursorW(nullptr,
            guide.angularDrag                                            ? IDC_CROSS
            : guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE
                                                                         : IDC_SIZENS));
        return true;
    }
    if (activeLayoutDrag_.has_value()) {
        const auto& guide = activeLayoutDrag_->guide;
        SetCursor(
            LoadCursorW(nullptr, guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
        return true;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);
    RefreshHover(RenderPoint{cursor.x, cursor.y});
    return true;
}

std::optional<LayoutEditController::TooltipTarget> LayoutEditController::CurrentTooltipTarget() {
    if (activeLayoutDrag_.has_value()) {
        TooltipTarget target;
        target.kind = TooltipTarget::Kind::LayoutGuide;
        target.clientPoint = lastClientPoint_;
        target.layoutGuide = activeLayoutDrag_->guide;
        return target;
    }

    if (activeWidgetEditDrag_.has_value()) {
        TooltipTarget target;
        target.kind = TooltipTarget::Kind::WidgetGuide;
        target.clientPoint = lastClientPoint_;
        target.widgetGuide = activeWidgetEditDrag_->guide;
        return target;
    }

    DashboardRenderer& renderer = host_.LayoutEditRenderer();
    if (activeAnchorEditDrag_.has_value()) {
        const auto region = renderer.FindEditableAnchorRegion(activeAnchorEditDrag_->key);
        if (region.has_value()) {
            TooltipTarget target;
            target.kind = TooltipTarget::Kind::EditableAnchor;
            target.clientPoint = lastClientPoint_;
            target.editableAnchor = *region;
            return target;
        }
    }

    const HoverResolution resolution = ResolveHover(lastClientPoint_);
    if (resolution.actionableAnchorHandle.has_value()) {
        const auto region = renderer.FindEditableAnchorRegion(*resolution.actionableAnchorHandle);
        if (region.has_value()) {
            TooltipTarget target;
            target.kind = TooltipTarget::Kind::EditableAnchor;
            target.clientPoint = lastClientPoint_;
            target.editableAnchor = *region;
            return target;
        }
    }

    if (resolution.hoveredWidgetEditGuideIndex.has_value()) {
        const auto& guides = renderer.WidgetEditGuides();
        if (*resolution.hoveredWidgetEditGuideIndex < guides.size()) {
            TooltipTarget target;
            target.kind = TooltipTarget::Kind::WidgetGuide;
            target.clientPoint = lastClientPoint_;
            target.widgetGuide = guides[*resolution.hoveredWidgetEditGuideIndex];
            return target;
        }
    }

    if (resolution.hoveredLayoutGuideIndex.has_value()) {
        const auto& guides = renderer.LayoutEditGuides();
        if (*resolution.hoveredLayoutGuideIndex < guides.size()) {
            TooltipTarget target;
            target.kind = TooltipTarget::Kind::LayoutGuide;
            target.clientPoint = lastClientPoint_;
            target.layoutGuide = guides[*resolution.hoveredLayoutGuideIndex];
            return target;
        }
    }

    return std::nullopt;
}

void LayoutEditController::SyncRendererInteractionState() {
    DashboardRenderer::EditOverlayState& overlayState = host_.LayoutEditOverlayState();
    overlayState.hoveredEditableWidget = hoveredEditableWidget_;
    overlayState.hoveredEditableAnchor = hoveredEditableAnchor_;
    overlayState.activeLayoutEditGuide =
        activeLayoutDrag_.has_value() ? std::optional<DashboardRenderer::LayoutEditGuide>(activeLayoutDrag_->guide)
                                      : std::nullopt;
    overlayState.activeWidgetEditGuide =
        activeWidgetEditDrag_.has_value()
            ? std::optional<DashboardRenderer::WidgetEditGuide>(activeWidgetEditDrag_->guide)
            : std::nullopt;
    overlayState.activeEditableAnchor =
        activeAnchorEditDrag_.has_value()
            ? std::optional<DashboardRenderer::EditableAnchorKey>(activeAnchorEditDrag_->key)
            : std::nullopt;
}

void LayoutEditController::ClearInteractionState() {
    hoveredLayoutGuideIndex_.reset();
    hoveredEditableWidget_.reset();
    hoveredWidgetEditGuideIndex_.reset();
    hoveredEditableAnchor_.reset();
    host_.LayoutEditRenderer().SetLayoutGuideDragActive(false);
    host_.LayoutEditRenderer().SetInteractiveDragTraceActive(false);
    activeLayoutDrag_.reset();
    activeWidgetEditDrag_.reset();
    activeAnchorEditDrag_.reset();
}

void LayoutEditController::SetCursorForPoint(RenderPoint clientPoint) {
    const HoverResolution resolution = ResolveHover(clientPoint);
    if (resolution.actionableAnchorHandle.has_value()) {
        const auto region = host_.LayoutEditRenderer().FindEditableAnchorRegion(*resolution.actionableAnchorHandle);
        const auto dragAxis = region.has_value() ? region->dragAxis : DashboardRenderer::AnchorDragAxis::Vertical;
        const auto dragMode = region.has_value() ? region->dragMode : DashboardRenderer::AnchorDragMode::AxisDelta;
        SetCursor(LoadCursorW(nullptr,
            dragMode == DashboardRenderer::AnchorDragMode::RadialDistance ? IDC_SIZEALL
            : dragAxis == DashboardRenderer::AnchorDragAxis::Both         ? IDC_SIZEALL
            : dragAxis == DashboardRenderer::AnchorDragAxis::Vertical     ? IDC_SIZEWE
                                                                          : IDC_SIZENS));
        return;
    }

    if (resolution.hoveredWidgetEditGuideIndex.has_value()) {
        const auto& guides = host_.LayoutEditRenderer().WidgetEditGuides();
        const DashboardRenderer::WidgetEditGuide& widgetGuide = guides[*resolution.hoveredWidgetEditGuideIndex];
        SetCursor(LoadCursorW(nullptr,
            widgetGuide.angularDrag                                            ? IDC_CROSS
            : widgetGuide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE
                                                                               : IDC_SIZENS));
        return;
    }

    if (resolution.hoveredLayoutGuideIndex.has_value()) {
        const auto& guides = host_.LayoutEditRenderer().LayoutEditGuides();
        const DashboardRenderer::LayoutEditGuide& layoutGuide = guides[*resolution.hoveredLayoutGuideIndex];
        SetCursor(LoadCursorW(
            nullptr, layoutGuide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
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
               WidgetIdentityEquals(drag.snapCandidates[candidateIndex].widget, widget)) {
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
    const int currentCoordinate =
        drag.guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y;
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
    const auto guideIt =
        std::find_if(guides.begin(), guides.end(), [&](const DashboardRenderer::LayoutEditGuide& candidate) {
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
        const int currentCoordinate =
            drag.guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y;
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
    const auto guideIt =
        std::find_if(guides.begin(), guides.end(), [&](const DashboardRenderer::WidgetEditGuide& candidate) {
            return candidate.parameter == drag.guide.parameter && candidate.guideId == drag.guide.guideId &&
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

bool LayoutEditController::UpdateAnchorEditDrag(RenderPoint clientPoint) {
    AnchorEditDragState& drag = *activeAnchorEditDrag_;
    int logicalDelta = 0;
    if (drag.dragMode == DashboardRenderer::AnchorDragMode::RadialDistance) {
        const double dx = static_cast<double>(clientPoint.x - drag.dragOrigin.x);
        const double dy = static_cast<double>(clientPoint.y - drag.dragOrigin.y);
        const double distanceDeltaPixels = std::sqrt((dx * dx) + (dy * dy)) - drag.dragStartDistancePixels;
        logicalDelta = static_cast<int>(std::lround(
            distanceDeltaPixels * drag.dragScale / (std::max)(0.1, host_.LayoutEditRenderer().RenderScale())));
    } else {
        int pixelDelta = 0;
        double scaleDivisor = drag.dragAxis == DashboardRenderer::AnchorDragAxis::Vertical ? 4.0 : 1.0;
        if (drag.dragAxis == DashboardRenderer::AnchorDragAxis::Both) {
            pixelDelta = (clientPoint.x - drag.dragStartPoint.x) + (clientPoint.y - drag.dragStartPoint.y);
            scaleDivisor = 4.0;
        } else {
            const int currentCoordinate =
                drag.dragAxis == DashboardRenderer::AnchorDragAxis::Vertical ? clientPoint.x : clientPoint.y;
            const int startCoordinate = drag.dragAxis == DashboardRenderer::AnchorDragAxis::Vertical
                                            ? drag.dragStartPoint.x
                                            : drag.dragStartPoint.y;
            pixelDelta = currentCoordinate - startCoordinate;
        }
        logicalDelta =
            static_cast<int>(std::lround(static_cast<double>(pixelDelta) /
                                         (std::max)(0.1, host_.LayoutEditRenderer().RenderScale() * scaleDivisor)));
    }
    const int nextValue = (std::max)(1, drag.initialValue + logicalDelta);
    const bool updated = host_.ApplyLayoutEditValue(drag.key.parameter, static_cast<double>(nextValue));
    if (updated) {
        SyncRendererInteractionState();
    }
    return updated;
}
