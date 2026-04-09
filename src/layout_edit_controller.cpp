#include "layout_edit_controller.h"

#include <algorithm>
#include <cmath>

#include "app_shared.h"
#include "layout_snap_solver.h"

namespace {

bool WidgetIdentityEquals(const DashboardRenderer::LayoutWidgetIdentity& left,
    const DashboardRenderer::LayoutWidgetIdentity& right) {
    return left.renderCardId == right.renderCardId &&
        left.editCardId == right.editCardId &&
        left.nodePath == right.nodePath;
}

bool EditableTextKeyEquals(const DashboardRenderer::EditableTextKey& left,
    const DashboardRenderer::EditableTextKey& right) {
    return WidgetIdentityEquals(left.widget, right.widget) &&
        left.fontRole == right.fontRole &&
        left.textId == right.textId;
}

bool EditableBarKeyEquals(const DashboardRenderer::EditableBarKey& left,
    const DashboardRenderer::EditableBarKey& right) {
    return WidgetIdentityEquals(left.widget, right.widget) &&
        left.parameter == right.parameter &&
        left.barId == right.barId;
}

bool EditableGaugeKeyEquals(const DashboardRenderer::EditableGaugeKey& left,
    const DashboardRenderer::EditableGaugeKey& right) {
    return WidgetIdentityEquals(left.widget, right.widget) &&
        left.parameter == right.parameter &&
        left.anchorId == right.anchorId;
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

std::optional<double> ComputeGaugeSegmentGapDegrees(const DashboardRenderer::WidgetEditGuide& guide, POINT clientPoint) {
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

LayoutEditHost::ValueTarget LayoutEditHost::ValueTarget::ForWidgetGuide(const DashboardRenderer::WidgetEditGuide& guide) {
    ValueTarget target;
    switch (guide.parameter) {
    case DashboardRenderer::WidgetEditParameter::MetricListLabelWidth:
        target.field = Field::MetricListLabelWidth;
        break;
    case DashboardRenderer::WidgetEditParameter::MetricListVerticalGap:
        target.field = Field::MetricListVerticalGap;
        break;
    case DashboardRenderer::WidgetEditParameter::DriveUsageActivityWidth:
        target.field = Field::DriveUsageActivityWidth;
        break;
    case DashboardRenderer::WidgetEditParameter::DriveUsageFreeWidth:
        target.field = Field::DriveUsageFreeWidth;
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

LayoutEditHost::ValueTarget LayoutEditHost::ValueTarget::ForEditableText(const DashboardRenderer::EditableTextKey& key) {
    ValueTarget target;
    switch (key.fontRole) {
    case DashboardRenderer::FontRole::Title:
        target.field = Field::FontTitle;
        break;
    case DashboardRenderer::FontRole::Big:
        target.field = Field::FontBig;
        break;
    case DashboardRenderer::FontRole::Value:
        target.field = Field::FontValue;
        break;
    case DashboardRenderer::FontRole::Label:
        target.field = Field::FontLabel;
        break;
    case DashboardRenderer::FontRole::Text:
        target.field = Field::FontText;
        break;
    case DashboardRenderer::FontRole::Small:
        target.field = Field::FontSmall;
        break;
    case DashboardRenderer::FontRole::Footer:
        target.field = Field::FontFooter;
        break;
    case DashboardRenderer::FontRole::ClockTime:
        target.field = Field::FontClockTime;
        break;
    case DashboardRenderer::FontRole::ClockDate:
        target.field = Field::FontClockDate;
        break;
    default:
        target.field = Field::FontLabel;
        break;
    }
    return target;
}

LayoutEditHost::ValueTarget LayoutEditHost::ValueTarget::ForEditableBar(const DashboardRenderer::EditableBarKey& key) {
    ValueTarget target;
    switch (key.parameter) {
    case DashboardRenderer::BarEditParameter::MetricListBarHeight:
        target.field = Field::MetricListBarHeight;
        break;
    case DashboardRenderer::BarEditParameter::DriveUsageBarHeight:
        target.field = Field::DriveUsageBarHeight;
        break;
    default:
        target.field = Field::MetricListBarHeight;
        break;
    }
    return target;
}

LayoutEditHost::ValueTarget LayoutEditHost::ValueTarget::ForEditableGauge(const DashboardRenderer::EditableGaugeKey& key) {
    ValueTarget target;
    switch (key.parameter) {
    case DashboardRenderer::GaugeAnchorParameter::SegmentCount:
        target.field = Field::GaugeSegmentCount;
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

void LayoutEditController::StopSession(HWND hwnd, bool showLayoutEditGuidesAfterStop) {
    (void) hwnd;
    ClearInteractionState();
    host_.LayoutEditOverlayState().showLayoutEditGuides = showLayoutEditGuidesAfterStop;
    SyncRendererInteractionState();
    ReleaseCapture();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    host_.InvalidateLayoutEdit();
}

const DashboardRenderer::LayoutEditGuide* LayoutEditController::HitTestLayoutGuide(POINT clientPoint, size_t* index) const {
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

const DashboardRenderer::WidgetEditGuide* LayoutEditController::HitTestWidgetEditGuide(POINT clientPoint, size_t* index) const {
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

void LayoutEditController::RefreshHover(HWND hwnd, POINT clientPoint) {
    if (activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() ||
        activeTextEditDrag_.has_value() || activeBarEditDrag_.has_value() || activeGaugeEditDrag_.has_value()) {
        return;
    }

    DashboardRenderer& renderer = host_.LayoutEditRenderer();
    const std::optional<DashboardRenderer::EditableGaugeKey> nextHoveredGaugeAnchor = renderer.HitTestEditableGaugeAnchor(clientPoint);
    const std::optional<DashboardRenderer::EditableBarKey> nextHoveredBarAnchor = renderer.HitTestEditableBarAnchor(clientPoint);
    std::optional<DashboardRenderer::EditableBarKey> nextHoveredBar = nextHoveredBarAnchor;
    if (!nextHoveredBar.has_value()) {
        nextHoveredBar = renderer.HitTestEditableBar(clientPoint);
    }
    const std::optional<DashboardRenderer::EditableTextKey> nextHoveredTextAnchor = renderer.HitTestEditableTextAnchor(clientPoint);
    std::optional<DashboardRenderer::EditableTextKey> nextHoveredText = nextHoveredTextAnchor;
    if (!nextHoveredText.has_value()) {
        nextHoveredText = renderer.HitTestEditableText(clientPoint);
    }
    const std::optional<DashboardRenderer::LayoutWidgetIdentity> nextHoveredWidget = nextHoveredGaugeAnchor.has_value()
        ? std::optional<DashboardRenderer::LayoutWidgetIdentity>(nextHoveredGaugeAnchor->widget)
        : nextHoveredBar.has_value()
        ? std::optional<DashboardRenderer::LayoutWidgetIdentity>(nextHoveredBar->widget)
        : nextHoveredText.has_value()
        ? std::optional<DashboardRenderer::LayoutWidgetIdentity>(nextHoveredText->widget)
        : renderer.HitTestEditableWidget(clientPoint);

    bool hoverChanged = (hoveredEditableWidget_.has_value() != nextHoveredWidget.has_value());
    if (!hoverChanged && hoveredEditableWidget_.has_value() && nextHoveredWidget.has_value()) {
        hoverChanged = !WidgetIdentityEquals(*hoveredEditableWidget_, *nextHoveredWidget);
    }
    if (hoverChanged) {
        hoveredEditableWidget_ = nextHoveredWidget;
        host_.LayoutEditOverlayState().hoveredEditableWidget = hoveredEditableWidget_;
    }
    if (hoveredEditableText_.has_value() != nextHoveredText.has_value() ||
        (hoveredEditableText_.has_value() && nextHoveredText.has_value() &&
            !EditableTextKeyEquals(*hoveredEditableText_, *nextHoveredText))) {
        hoveredEditableText_ = nextHoveredText;
        host_.LayoutEditOverlayState().hoveredEditableText = hoveredEditableText_;
        hoverChanged = true;
    }
    if (hoveredEditableTextAnchor_.has_value() != nextHoveredTextAnchor.has_value() ||
        (hoveredEditableTextAnchor_.has_value() && nextHoveredTextAnchor.has_value() &&
            !EditableTextKeyEquals(*hoveredEditableTextAnchor_, *nextHoveredTextAnchor))) {
        hoveredEditableTextAnchor_ = nextHoveredTextAnchor;
        hoverChanged = true;
    }
    if (hoveredEditableBar_.has_value() != nextHoveredBar.has_value() ||
        (hoveredEditableBar_.has_value() && nextHoveredBar.has_value() &&
            !EditableBarKeyEquals(*hoveredEditableBar_, *nextHoveredBar))) {
        hoveredEditableBar_ = nextHoveredBar;
        host_.LayoutEditOverlayState().hoveredEditableBar = hoveredEditableBar_;
        hoverChanged = true;
    }
    if (hoveredEditableBarAnchor_.has_value() != nextHoveredBarAnchor.has_value() ||
        (hoveredEditableBarAnchor_.has_value() && nextHoveredBarAnchor.has_value() &&
            !EditableBarKeyEquals(*hoveredEditableBarAnchor_, *nextHoveredBarAnchor))) {
        hoveredEditableBarAnchor_ = nextHoveredBarAnchor;
        hoverChanged = true;
    }
    if (hoveredEditableGaugeAnchor_.has_value() != nextHoveredGaugeAnchor.has_value() ||
        (hoveredEditableGaugeAnchor_.has_value() && nextHoveredGaugeAnchor.has_value() &&
            !EditableGaugeKeyEquals(*hoveredEditableGaugeAnchor_, *nextHoveredGaugeAnchor))) {
        hoveredEditableGaugeAnchor_ = nextHoveredGaugeAnchor;
        host_.LayoutEditOverlayState().hoveredEditableGauge = hoveredEditableGaugeAnchor_;
        hoverChanged = true;
    }

    size_t widgetGuideIndex = 0;
    const DashboardRenderer::WidgetEditGuide* widgetGuide = nullptr;
    if (hoveredEditableWidget_.has_value()) {
        widgetGuide = HitTestWidgetEditGuide(clientPoint, &widgetGuideIndex);
        if (widgetGuide != nullptr && !WidgetIdentityEquals(widgetGuide->widget, *hoveredEditableWidget_)) {
            widgetGuide = nullptr;
        }
    }
    const std::optional<size_t> nextWidgetGuideIndex = widgetGuide != nullptr
        ? std::optional<size_t>(widgetGuideIndex)
        : std::nullopt;
    if (hoveredWidgetEditGuideIndex_ != nextWidgetGuideIndex) {
        hoveredWidgetEditGuideIndex_ = nextWidgetGuideIndex;
        hoverChanged = true;
    }

    size_t layoutGuideIndex = 0;
    const DashboardRenderer::LayoutEditGuide* layoutGuide = HitTestLayoutGuide(clientPoint, &layoutGuideIndex);
    const std::optional<size_t> nextLayoutGuideIndex = layoutGuide != nullptr
        ? std::optional<size_t>(layoutGuideIndex)
        : std::nullopt;
    if (hoveredLayoutGuideIndex_ != nextLayoutGuideIndex) {
        hoveredLayoutGuideIndex_ = nextLayoutGuideIndex;
        hoverChanged = true;
    }

    if (hoverChanged) {
        host_.InvalidateLayoutEdit();
    }

    SetCursorForPoint(hwnd, clientPoint);
}

bool LayoutEditController::HandleLButtonDown(HWND hwnd, POINT clientPoint) {
    DashboardRenderer& renderer = host_.LayoutEditRenderer();
    if (hoveredEditableGaugeAnchor_.has_value()) {
        const auto region = renderer.FindEditableGaugeRegion(*hoveredEditableGaugeAnchor_);
        if (region.has_value()) {
            activeGaugeEditDrag_ = GaugeEditDragState{region->key, region->value, clientPoint.x};
            hoveredEditableWidget_ = region->key.widget;
            SyncRendererInteractionState();
            SetCapture(hwnd);
            return true;
        }
    }
    if (hoveredEditableBarAnchor_.has_value()) {
        const auto region = renderer.FindEditableBarRegion(*hoveredEditableBarAnchor_);
        if (region.has_value()) {
            activeBarEditDrag_ = BarEditDragState{region->key, region->value, clientPoint.y};
            hoveredEditableBar_ = region->key;
            hoveredEditableWidget_ = region->key.widget;
            SyncRendererInteractionState();
            SetCapture(hwnd);
            return true;
        }
    }
    if (hoveredEditableTextAnchor_.has_value()) {
        const auto region = renderer.FindEditableTextRegion(*hoveredEditableTextAnchor_);
        if (region.has_value()) {
            activeTextEditDrag_ = TextEditDragState{region->key, region->fontSize, clientPoint.x};
            hoveredEditableText_ = region->key;
            hoveredEditableWidget_ = region->key.widget;
            SyncRendererInteractionState();
            SetCapture(hwnd);
            return true;
        }
    }

    size_t widgetGuideIndex = 0;
    const DashboardRenderer::WidgetEditGuide* widgetGuide = HitTestWidgetEditGuide(clientPoint, &widgetGuideIndex);
    if (widgetGuide != nullptr) {
        activeWidgetEditDrag_ = WidgetEditDragState{
            *widgetGuide,
            widgetGuide->value,
            widgetGuide->axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y,
        };
        hoveredEditableWidget_ = widgetGuide->widget;
        hoveredWidgetEditGuideIndex_ = widgetGuideIndex;
        SyncRendererInteractionState();
        SetCapture(hwnd);
        return true;
    }

    size_t guideIndex = 0;
    const DashboardRenderer::LayoutEditGuide* guide = HitTestLayoutGuide(clientPoint, &guideIndex);
    if (guide != nullptr) {
        const LayoutNodeConfig* guideNode = layout_edit::FindGuideNode(
            host_.LayoutEditConfig(), LayoutEditHost::LayoutTarget::ForGuide(*guide));
        const std::vector<int> initialWeights = layout_edit::SeedGuideWeights(*guide, guideNode);
        activeLayoutDrag_ = LayoutDragState{
            *guide,
            initialWeights,
            renderer.CollectLayoutGuideSnapCandidates(*guide),
            guide->axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y,
        };
        hoveredLayoutGuideIndex_ = guideIndex;
        SyncRendererInteractionState();
        SetCapture(hwnd);
        return true;
    }

    return false;
}

bool LayoutEditController::HandleMouseMove(HWND hwnd, POINT clientPoint) {
    if (activeLayoutDrag_.has_value()) {
        return UpdateLayoutDrag(hwnd, clientPoint);
    }
    if (activeGaugeEditDrag_.has_value()) {
        return UpdateGaugeEditDrag(clientPoint);
    }
    if (activeBarEditDrag_.has_value()) {
        return UpdateBarEditDrag(clientPoint);
    }
    if (activeTextEditDrag_.has_value()) {
        return UpdateTextEditDrag(clientPoint);
    }
    if (activeWidgetEditDrag_.has_value()) {
        return UpdateWidgetEditDrag(clientPoint);
    }

    RefreshHover(hwnd, clientPoint);
    return true;
}

bool LayoutEditController::HandleLButtonUp(HWND hwnd, POINT clientPoint) {
    bool released = false;
    if (activeGaugeEditDrag_.has_value()) {
        activeGaugeEditDrag_.reset();
        released = true;
    } else if (activeBarEditDrag_.has_value()) {
        activeBarEditDrag_.reset();
        released = true;
    } else if (activeTextEditDrag_.has_value()) {
        activeTextEditDrag_.reset();
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
    RefreshHover(hwnd, clientPoint);
    return true;
}

bool LayoutEditController::HandleCaptureChanged(HWND hwnd, HWND newCaptureOwner) {
    if (newCaptureOwner == hwnd) {
        return false;
    }

    const bool hadActiveDrag = activeGaugeEditDrag_.has_value() || activeBarEditDrag_.has_value() ||
        activeTextEditDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeLayoutDrag_.has_value();
    if (!hadActiveDrag) {
        return false;
    }

    activeGaugeEditDrag_.reset();
    activeBarEditDrag_.reset();
    activeTextEditDrag_.reset();
    activeWidgetEditDrag_.reset();
    activeLayoutDrag_.reset();
    SyncRendererInteractionState();
    host_.InvalidateLayoutEdit();
    return true;
}

bool LayoutEditController::HandleSetCursor(HWND hwnd) {
    if (activeGaugeEditDrag_.has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return true;
    }
    if (activeBarEditDrag_.has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
        return true;
    }
    if (activeTextEditDrag_.has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return true;
    }
    if (activeWidgetEditDrag_.has_value()) {
        const auto& guide = activeWidgetEditDrag_->guide;
        SetCursor(LoadCursorW(nullptr,
            guide.angularDrag ? IDC_CROSS
            : guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
        return true;
    }
    if (activeLayoutDrag_.has_value()) {
        const auto& guide = activeLayoutDrag_->guide;
        SetCursor(LoadCursorW(nullptr,
            guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
        return true;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);
    RefreshHover(hwnd, cursor);
    return true;
}

void LayoutEditController::SyncRendererInteractionState() {
    DashboardRenderer::EditOverlayState& overlayState = host_.LayoutEditOverlayState();
    overlayState.hoveredEditableWidget = hoveredEditableWidget_;
    overlayState.hoveredEditableText = hoveredEditableText_;
    overlayState.hoveredEditableBar = hoveredEditableBar_;
    overlayState.hoveredEditableGauge = hoveredEditableGaugeAnchor_;
    overlayState.activeLayoutEditGuide = activeLayoutDrag_.has_value()
        ? std::optional<DashboardRenderer::LayoutEditGuide>(activeLayoutDrag_->guide)
        : std::nullopt;
    overlayState.activeWidgetEditGuide = activeWidgetEditDrag_.has_value()
        ? std::optional<DashboardRenderer::WidgetEditGuide>(activeWidgetEditDrag_->guide)
        : std::nullopt;
    overlayState.activeEditableText = activeTextEditDrag_.has_value()
        ? std::optional<DashboardRenderer::EditableTextKey>(activeTextEditDrag_->key)
        : std::nullopt;
    overlayState.activeEditableBar = activeBarEditDrag_.has_value()
        ? std::optional<DashboardRenderer::EditableBarKey>(activeBarEditDrag_->key)
        : std::nullopt;
    overlayState.activeEditableGauge = activeGaugeEditDrag_.has_value()
        ? std::optional<DashboardRenderer::EditableGaugeKey>(activeGaugeEditDrag_->key)
        : std::nullopt;
}

void LayoutEditController::ClearInteractionState() {
    hoveredLayoutGuideIndex_.reset();
    hoveredEditableWidget_.reset();
    hoveredWidgetEditGuideIndex_.reset();
    hoveredEditableText_.reset();
    hoveredEditableTextAnchor_.reset();
    hoveredEditableBar_.reset();
    hoveredEditableBarAnchor_.reset();
    hoveredEditableGaugeAnchor_.reset();
    activeLayoutDrag_.reset();
    activeWidgetEditDrag_.reset();
    activeTextEditDrag_.reset();
    activeBarEditDrag_.reset();
    activeGaugeEditDrag_.reset();
}

void LayoutEditController::SetCursorForPoint(HWND hwnd, POINT clientPoint) {
    (void) hwnd;
    if (hoveredEditableTextAnchor_.has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return;
    }
    if (hoveredEditableGaugeAnchor_.has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return;
    }
    if (hoveredEditableBarAnchor_.has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
        return;
    }

    const DashboardRenderer::WidgetEditGuide* widgetGuide = nullptr;
    if (hoveredEditableWidget_.has_value()) {
        widgetGuide = HitTestWidgetEditGuide(clientPoint);
        if (widgetGuide != nullptr && !WidgetIdentityEquals(widgetGuide->widget, *hoveredEditableWidget_)) {
            widgetGuide = nullptr;
        }
    }
    if (widgetGuide != nullptr) {
        SetCursor(LoadCursorW(nullptr,
            widgetGuide->angularDrag ? IDC_CROSS
            : widgetGuide->axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
        return;
    }

    const DashboardRenderer::LayoutEditGuide* layoutGuide = HitTestLayoutGuide(clientPoint);
    if (layoutGuide != nullptr) {
        SetCursor(LoadCursorW(nullptr,
            layoutGuide->axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
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
        const auto snappedWeight = layout_snap_solver::FindNearestSnapWeight(
            freeWeights[index], combined, threshold, {layout_snap_solver::SnapCandidate{
                candidate.targetExtent,
                candidate.startDistance,
                candidate.groupOrder,
            }}, [&](int firstWeight) -> std::optional<int> {
                std::vector<int> attemptWeights = freeWeights;
                attemptWeights[index] = firstWeight;
                attemptWeights[index + 1] = combined - firstWeight;
                return host_.EvaluateLayoutWidgetExtentForWeights(
                    LayoutEditHost::LayoutTarget::ForGuide(drag.guide), attemptWeights, candidate.widget, drag.guide.axis);
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

bool LayoutEditController::UpdateLayoutDrag(HWND hwnd, POINT clientPoint) {
    LayoutDragState& drag = *activeLayoutDrag_;
    const int currentCoordinate = drag.guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y;
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
    const auto guideIt = std::find_if(guides.begin(), guides.end(), [&](const DashboardRenderer::LayoutEditGuide& candidate) {
        return candidate.renderCardId == drag.guide.renderCardId &&
            candidate.editCardId == drag.guide.editCardId &&
            candidate.nodePath == drag.guide.nodePath &&
            candidate.separatorIndex == drag.guide.separatorIndex;
    });
    if (guideIt != guides.end()) {
        drag.guide = *guideIt;
    }

    SyncRendererInteractionState();
    RefreshHover(hwnd, clientPoint);
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
        const int currentCoordinate = drag.guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y;
        const int pixelDelta = currentCoordinate - drag.dragStartCoordinate;
        const int logicalDelta = static_cast<int>(std::lround(
            static_cast<double>(pixelDelta * drag.guide.dragDirection) /
            (std::max)(0.1, host_.LayoutEditRenderer().RenderScale())));
        nextValue = (std::max)(1.0, drag.initialValue + static_cast<double>(logicalDelta));
    }
    if (!host_.ApplyLayoutEditValue(LayoutEditHost::ValueTarget::ForWidgetGuide(drag.guide), nextValue)) {
        return false;
    }

    const auto& guides = host_.LayoutEditRenderer().WidgetEditGuides();
    const auto guideIt = std::find_if(guides.begin(), guides.end(), [&](const DashboardRenderer::WidgetEditGuide& candidate) {
        return candidate.parameter == drag.guide.parameter &&
            candidate.guideId == drag.guide.guideId &&
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

bool LayoutEditController::UpdateTextEditDrag(POINT clientPoint) {
    TextEditDragState& drag = *activeTextEditDrag_;
    const int pixelDelta = clientPoint.x - drag.dragStartCoordinate;
    const int logicalDelta = static_cast<int>(std::lround(
        static_cast<double>(pixelDelta) / (std::max)(0.1, host_.LayoutEditRenderer().RenderScale())));
    const int nextValue = (std::max)(1, drag.initialValue + logicalDelta);
    const bool updated = host_.ApplyLayoutEditValue(LayoutEditHost::ValueTarget::ForEditableText(drag.key),
        static_cast<double>(nextValue));
    if (updated) {
        SyncRendererInteractionState();
    }
    return updated;
}

bool LayoutEditController::UpdateBarEditDrag(POINT clientPoint) {
    BarEditDragState& drag = *activeBarEditDrag_;
    const int pixelDelta = clientPoint.y - drag.dragStartCoordinate;
    const int logicalDelta = static_cast<int>(std::lround(
        static_cast<double>(pixelDelta) / (std::max)(0.1, host_.LayoutEditRenderer().RenderScale())));
    const int nextValue = (std::max)(1, drag.initialValue + logicalDelta);
    const bool updated = host_.ApplyLayoutEditValue(LayoutEditHost::ValueTarget::ForEditableBar(drag.key),
        static_cast<double>(nextValue));
    if (updated) {
        SyncRendererInteractionState();
    }
    return updated;
}

bool LayoutEditController::UpdateGaugeEditDrag(POINT clientPoint) {
    GaugeEditDragState& drag = *activeGaugeEditDrag_;
    const int pixelDelta = clientPoint.x - drag.dragStartCoordinate;
    const int logicalDelta = static_cast<int>(std::lround(
        static_cast<double>(pixelDelta) / (std::max)(0.1, host_.LayoutEditRenderer().RenderScale() * 4.0)));
    const int nextValue = (std::max)(1, drag.initialValue + logicalDelta);
    const bool updated = host_.ApplyLayoutEditValue(LayoutEditHost::ValueTarget::ForEditableGauge(drag.key),
        static_cast<double>(nextValue));
    if (updated) {
        SyncRendererInteractionState();
    }
    return updated;
}
