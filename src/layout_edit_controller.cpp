#include "layout_edit_controller.h"

#include <algorithm>
#include <cmath>

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

bool IsFontAnchorParameter(DashboardRenderer::AnchorEditParameter parameter) {
    using Parameter = DashboardRenderer::AnchorEditParameter;
    switch (parameter) {
        case Parameter::FontTitle:
        case Parameter::FontBig:
        case Parameter::FontValue:
        case Parameter::FontLabel:
        case Parameter::FontText:
        case Parameter::FontSmall:
        case Parameter::FontFooter:
        case Parameter::FontClockTime:
        case Parameter::FontClockDate:
            return true;
        default:
            return false;
    }
}

double NormalizeDegrees(double degrees) {
    double normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    return normalized;
}

std::optional<double> ComputeGaugeSweepDegrees(POINT origin, POINT clientPoint) {
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

std::optional<double> ComputeGaugePointerAngle(POINT origin, POINT clientPoint) {
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

std::optional<double> ComputeGaugeSegmentGapDegrees(
    const DashboardRenderer::WidgetEditGuide& guide, POINT clientPoint) {
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

LayoutEditHost::ValueTarget LayoutEditHost::ValueTarget::ForWidgetGuide(
    const DashboardRenderer::WidgetEditGuide& guide) {
    ValueTarget target;
    switch (guide.parameter) {
        case DashboardRenderer::WidgetEditParameter::MetricListLabelWidth:
            target.field = Field::MetricListLabelWidth;
            break;
        case DashboardRenderer::WidgetEditParameter::MetricListVerticalGap:
            target.field = Field::MetricListVerticalGap;
            break;
        case DashboardRenderer::WidgetEditParameter::DriveUsageLabelGap:
            target.field = Field::DriveUsageLabelGap;
            break;
        case DashboardRenderer::WidgetEditParameter::DriveUsageBarGap:
            target.field = Field::DriveUsageBarGap;
            break;
        case DashboardRenderer::WidgetEditParameter::DriveUsageRwGap:
            target.field = Field::DriveUsageRwGap;
            break;
        case DashboardRenderer::WidgetEditParameter::DriveUsagePercentGap:
            target.field = Field::DriveUsagePercentGap;
            break;
        case DashboardRenderer::WidgetEditParameter::DriveUsageActivityWidth:
            target.field = Field::DriveUsageActivityWidth;
            break;
        case DashboardRenderer::WidgetEditParameter::DriveUsageFreeWidth:
            target.field = Field::DriveUsageFreeWidth;
            break;
        case DashboardRenderer::WidgetEditParameter::DriveUsageActivitySegmentGap:
            target.field = Field::DriveUsageActivitySegmentGap;
            break;
        case DashboardRenderer::WidgetEditParameter::DriveUsageHeaderGap:
            target.field = Field::DriveUsageHeaderGap;
            break;
        case DashboardRenderer::WidgetEditParameter::DriveUsageRowGap:
            target.field = Field::DriveUsageRowGap;
            break;
        case DashboardRenderer::WidgetEditParameter::ThroughputAxisPadding:
            target.field = Field::ThroughputAxisPadding;
            break;
        case DashboardRenderer::WidgetEditParameter::ThroughputHeaderGap:
            target.field = Field::ThroughputHeaderGap;
            break;
        case DashboardRenderer::WidgetEditParameter::ThroughputGuideStrokeWidth:
            target.field = Field::ThroughputGuideStrokeWidth;
            break;
        case DashboardRenderer::WidgetEditParameter::ThroughputPlotStrokeWidth:
            target.field = Field::ThroughputPlotStrokeWidth;
            break;
        case DashboardRenderer::WidgetEditParameter::ThroughputLeaderDiameter:
            target.field = Field::ThroughputLeaderDiameter;
            break;
        case DashboardRenderer::WidgetEditParameter::GaugeOuterPadding:
            target.field = Field::GaugeOuterPadding;
            break;
        case DashboardRenderer::WidgetEditParameter::GaugeRingThickness:
            target.field = Field::GaugeRingThickness;
            break;
        case DashboardRenderer::WidgetEditParameter::GaugeValueBottom:
            target.field = Field::GaugeValueBottom;
            break;
        case DashboardRenderer::WidgetEditParameter::GaugeLabelBottom:
            target.field = Field::GaugeLabelBottom;
            break;
        case DashboardRenderer::WidgetEditParameter::GaugeSweepDegrees:
            target.field = Field::GaugeSweepDegrees;
            break;
        case DashboardRenderer::WidgetEditParameter::GaugeSegmentGapDegrees:
            target.field = Field::GaugeSegmentGapDegrees;
            break;
        default:
            target.field = Field::MetricListLabelWidth;
            break;
    }
    return target;
}

LayoutEditHost::ValueTarget LayoutEditHost::ValueTarget::ForEditableAnchor(
    const DashboardRenderer::EditableAnchorKey& key) {
    ValueTarget target;
    switch (key.parameter) {
        case DashboardRenderer::AnchorEditParameter::FontTitle:
            target.field = Field::FontTitle;
            break;
        case DashboardRenderer::AnchorEditParameter::FontBig:
            target.field = Field::FontBig;
            break;
        case DashboardRenderer::AnchorEditParameter::FontValue:
            target.field = Field::FontValue;
            break;
        case DashboardRenderer::AnchorEditParameter::FontLabel:
            target.field = Field::FontLabel;
            break;
        case DashboardRenderer::AnchorEditParameter::FontText:
            target.field = Field::FontText;
            break;
        case DashboardRenderer::AnchorEditParameter::FontSmall:
            target.field = Field::FontSmall;
            break;
        case DashboardRenderer::AnchorEditParameter::FontFooter:
            target.field = Field::FontFooter;
            break;
        case DashboardRenderer::AnchorEditParameter::FontClockTime:
            target.field = Field::FontClockTime;
            break;
        case DashboardRenderer::AnchorEditParameter::FontClockDate:
            target.field = Field::FontClockDate;
            break;
        case DashboardRenderer::AnchorEditParameter::MetricListBarHeight:
            target.field = Field::MetricListBarHeight;
            break;
        case DashboardRenderer::AnchorEditParameter::DriveUsageBarHeight:
            target.field = Field::DriveUsageBarHeight;
            break;
        case DashboardRenderer::AnchorEditParameter::SegmentCount:
            target.field = Field::GaugeSegmentCount;
            break;
        case DashboardRenderer::AnchorEditParameter::DriveUsageActivitySegments:
            target.field = Field::DriveUsageActivitySegments;
            break;
        case DashboardRenderer::AnchorEditParameter::ThroughputGuideStrokeWidth:
            target.field = Field::ThroughputGuideStrokeWidth;
            break;
        case DashboardRenderer::AnchorEditParameter::ThroughputPlotStrokeWidth:
            target.field = Field::ThroughputPlotStrokeWidth;
            break;
        case DashboardRenderer::AnchorEditParameter::ThroughputLeaderDiameter:
            target.field = Field::ThroughputLeaderDiameter;
            break;
        case DashboardRenderer::AnchorEditParameter::GaugeOuterPadding:
            target.field = Field::GaugeOuterPadding;
            break;
        case DashboardRenderer::AnchorEditParameter::GaugeRingThickness:
            target.field = Field::GaugeRingThickness;
            break;
        default:
            target.field = Field::GaugeSegmentCount;
            break;
    }
    return target;
}

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
    host_.InvalidateLayoutEdit();
}

const DashboardRenderer::LayoutEditGuide* LayoutEditController::HitTestLayoutGuide(
    POINT clientPoint, size_t* index) const {
    const auto& guides = host_.LayoutEditRenderer().LayoutEditGuides();
    for (size_t i = 0; i < guides.size(); ++i) {
        if (PtInRect(&guides[i].hitRect, clientPoint)) {
            if (index != nullptr) {
                *index = i;
            }
            return &guides[i];
        }
    }
    return nullptr;
}

const DashboardRenderer::WidgetEditGuide* LayoutEditController::HitTestWidgetEditGuide(
    POINT clientPoint, size_t* index) const {
    const auto& guides = host_.LayoutEditRenderer().WidgetEditGuides();
    for (size_t i = 0; i < guides.size(); ++i) {
        if (PtInRect(&guides[i].hitRect, clientPoint)) {
            if (index != nullptr) {
                *index = i;
            }
            return &guides[i];
        }
    }
    return nullptr;
}

LayoutEditController::HoverResolution LayoutEditController::ResolveHover(POINT clientPoint) const {
    HoverResolution resolution;
    DashboardRenderer& renderer = host_.LayoutEditRenderer();

    const std::optional<DashboardRenderer::EditableAnchorKey> anchorHandle = renderer.HitTestEditableAnchorHandle(clientPoint);
    if (anchorHandle.has_value()) {
        resolution.hoveredEditableAnchor = anchorHandle;
        resolution.actionableAnchorHandle = anchorHandle;
        resolution.hoveredEditableWidget = anchorHandle->widget;
        return resolution;
    }

    const std::optional<DashboardRenderer::EditableAnchorKey> anchorTarget = renderer.HitTestEditableAnchorTarget(clientPoint);
    if (anchorTarget.has_value() && IsFontAnchorParameter(anchorTarget->parameter)) {
        resolution.hoveredEditableAnchor = anchorTarget;
        resolution.hoveredEditableWidget = anchorTarget->widget;
        return resolution;
    }

    size_t widgetGuideIndex = 0;
    const DashboardRenderer::WidgetEditGuide* widgetGuide = HitTestWidgetEditGuide(clientPoint, &widgetGuideIndex);
    if (widgetGuide != nullptr) {
        resolution.hoveredEditableWidget = widgetGuide->widget;
        resolution.hoveredWidgetEditGuideIndex = widgetGuideIndex;
        return resolution;
    }

    const std::optional<DashboardRenderer::LayoutWidgetIdentity> hoveredWidget = renderer.HitTestEditableWidget(clientPoint);
    if (hoveredWidget.has_value()) {
        resolution.hoveredEditableWidget = hoveredWidget;

        if (anchorTarget.has_value() && WidgetIdentityEquals(anchorTarget->widget, *hoveredWidget)) {
            resolution.hoveredEditableAnchor = anchorTarget;
            return resolution;
        }

        return resolution;
    }

    size_t layoutGuideIndex = 0;
    if (HitTestLayoutGuide(clientPoint, &layoutGuideIndex) != nullptr) {
        resolution.hoveredLayoutGuideIndex = layoutGuideIndex;
        return resolution;
    }

    return resolution;
}

void LayoutEditController::RefreshHover(POINT clientPoint) {
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

bool LayoutEditController::HandleLButtonDown(HWND hwnd, POINT clientPoint) {
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
        hoveredLayoutGuideIndex_ = resolution.hoveredLayoutGuideIndex;
        SyncRendererInteractionState();
        SetCapture(hwnd);
        return true;
    }

    return false;
}

bool LayoutEditController::HandleMouseMove(POINT clientPoint) {
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

bool LayoutEditController::HandleLButtonUp(POINT clientPoint) {
    lastClientPoint_ = clientPoint;
    bool released = false;
    if (activeAnchorEditDrag_.has_value()) {
        activeAnchorEditDrag_.reset();
        released = true;
    } else if (activeWidgetEditDrag_.has_value()) {
        activeWidgetEditDrag_.reset();
        released = true;
    } else if (activeLayoutDrag_.has_value()) {
        activeLayoutDrag_.reset();
        released = true;
    }

    if (!released) {
        return false;
    }

    SyncRendererInteractionState();
    ReleaseCapture();
    RefreshHover(clientPoint);
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
    activeLayoutDrag_.reset();
    SyncRendererInteractionState();
    host_.InvalidateLayoutEdit();
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
    RefreshHover(cursor);
    return true;
}

std::optional<LayoutEditController::TooltipTarget> LayoutEditController::CurrentTooltipTarget() {
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

    if (resolution.hoveredEditableAnchor.has_value() && IsFontAnchorParameter(resolution.hoveredEditableAnchor->parameter)) {
        const auto region = renderer.FindEditableAnchorRegion(*resolution.hoveredEditableAnchor);
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
    activeLayoutDrag_.reset();
    activeWidgetEditDrag_.reset();
    activeAnchorEditDrag_.reset();
}

void LayoutEditController::SetCursorForPoint(POINT clientPoint) {
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
    const LayoutDragState& drag, const std::vector<int>& freeWeights) {
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

    for (const auto& candidate : drag.snapCandidates) {
        const auto snappedWeight = layout_snap_solver::FindNearestSnapWeight(freeWeights[index],
            combined,
            threshold,
            {layout_snap_solver::SnapCandidate{
                candidate.targetExtent,
                candidate.startDistance,
                candidate.groupOrder,
            }},
            [&](int firstWeight) -> std::optional<int> {
                std::vector<int> attemptWeights = freeWeights;
                attemptWeights[index] = firstWeight;
                attemptWeights[index + 1] = combined - firstWeight;
                return host_.EvaluateLayoutWidgetExtentForWeights(LayoutEditHost::LayoutTarget::ForGuide(drag.guide),
                    attemptWeights,
                    candidate.widget,
                    drag.guide.axis);
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

bool LayoutEditController::UpdateLayoutDrag(POINT clientPoint) {
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
        if (const auto snappedWeights = FindSnappedLayoutGuideWeights(drag, weights); snappedWeights.has_value()) {
            weights = *snappedWeights;
        }
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

bool LayoutEditController::UpdateWidgetEditDrag(POINT clientPoint) {
    WidgetEditDragState& drag = *activeWidgetEditDrag_;
    double nextValue = drag.initialValue;
    if (drag.guide.angularDrag) {
        switch (drag.guide.parameter) {
            case DashboardRenderer::WidgetEditParameter::GaugeSweepDegrees: {
                const auto sweepDegrees = ComputeGaugeSweepDegrees(drag.guide.dragOrigin, clientPoint);
                if (!sweepDegrees.has_value()) {
                    return true;
                }
                nextValue = *sweepDegrees;
                break;
            }
            case DashboardRenderer::WidgetEditParameter::GaugeSegmentGapDegrees: {
                const auto segmentGapDegrees = ComputeGaugeSegmentGapDegrees(drag.guide, clientPoint);
                if (!segmentGapDegrees.has_value()) {
                    return true;
                }
                nextValue = *segmentGapDegrees;
                break;
            }
            default:
                return false;
        }
    } else {
        const int currentCoordinate =
            drag.guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y;
        const int pixelDelta = currentCoordinate - drag.dragStartCoordinate;
        const int logicalDelta =
            static_cast<int>(std::lround(static_cast<double>(pixelDelta * drag.guide.dragDirection) /
                                         (std::max)(0.1, host_.LayoutEditRenderer().RenderScale())));
        nextValue = (std::max)(1.0, drag.initialValue + static_cast<double>(logicalDelta));
    }
    if (!host_.ApplyLayoutEditValue(LayoutEditHost::ValueTarget::ForWidgetGuide(drag.guide), nextValue)) {
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

bool LayoutEditController::UpdateAnchorEditDrag(POINT clientPoint) {
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
    const bool updated = host_.ApplyLayoutEditValue(
        LayoutEditHost::ValueTarget::ForEditableAnchor(drag.key), static_cast<double>(nextValue));
    if (updated) {
        SyncRendererInteractionState();
    }
    return updated;
}
