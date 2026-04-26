#include "layout_edit/layout_edit_hit_test.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <tuple>

#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_hit_priority.h"

namespace {

bool IsAnchorHandleKind(LayoutEditActiveRegionKind kind) {
    return kind == LayoutEditActiveRegionKind::StaticEditAnchorHandle ||
           kind == LayoutEditActiveRegionKind::DynamicEditAnchorHandle;
}

bool IsAnchorTargetKind(LayoutEditActiveRegionKind kind) {
    return kind == LayoutEditActiveRegionKind::StaticEditAnchorTarget ||
           kind == LayoutEditActiveRegionKind::DynamicEditAnchorTarget;
}

bool IsColorTargetKind(LayoutEditActiveRegionKind kind) {
    return kind == LayoutEditActiveRegionKind::StaticColorTarget ||
           kind == LayoutEditActiveRegionKind::DynamicColorTarget;
}

int AnchorHoverPriority(const LayoutEditAnchorRegion& region) {
    if (region.shape == AnchorShape::Square) {
        return -2;
    }
    if (region.shape == AnchorShape::Wedge) {
        return 2;
    }
    return LayoutEditAnchorHitPriority(region.key);
}

bool AnchorHandleContains(const LayoutEditAnchorRegion& region, RenderPoint clientPoint) {
    if (region.shape != AnchorShape::Circle) {
        return region.anchorHitRect.Contains(clientPoint);
    }

    const int width = std::max(1, region.anchorRect.right - region.anchorRect.left);
    const int height = std::max(1, region.anchorRect.bottom - region.anchorRect.top);
    const double radius = static_cast<double>(std::max(width, height)) / 2.0;
    const double centerX = static_cast<double>(region.anchorRect.left) + static_cast<double>(width) / 2.0;
    const double centerY = static_cast<double>(region.anchorRect.top) + static_cast<double>(height) / 2.0;
    const double dx = static_cast<double>(clientPoint.x) - centerX;
    const double dy = static_cast<double>(clientPoint.y) - centerY;
    const double distance = std::sqrt((dx * dx) + (dy * dy));
    return std::abs(distance - radius) <= static_cast<double>(region.anchorHitPadding);
}

long long RectArea(const RenderRect& rect) {
    const long long width = std::max<long long>(0, rect.right - rect.left);
    const long long height = std::max<long long>(0, rect.bottom - rect.top);
    return width * height;
}

int WidgetExtentForAxis(const LayoutEditWidgetRegion& widget, LayoutGuideAxis axis) {
    return axis == LayoutGuideAxis::Vertical ? std::max(0, static_cast<int>(widget.rect.right - widget.rect.left))
                                             : std::max(0, static_cast<int>(widget.rect.bottom - widget.rect.top));
}

bool IsWidgetAffectedByGuide(const LayoutEditWidgetRegion& widget, const LayoutEditGuide& guide) {
    if (!guide.renderCardId.empty() && widget.widget.renderCardId != guide.renderCardId) {
        return false;
    }
    return widget.rect.left >= guide.containerRect.left && widget.rect.top >= guide.containerRect.top &&
           widget.rect.right <= guide.containerRect.right && widget.rect.bottom <= guide.containerRect.bottom;
}

std::vector<LayoutEditWidgetRegion> CollectSimilarityIndicatorWidgets(
    const LayoutEditActiveRegions& regions, LayoutGuideAxis axis) {
    struct SimilarityRepresentativeKey {
        std::string cardId;
        WidgetClass widgetClass = WidgetClass::Unknown;
        int extent = 0;
        int edgeStart = 0;
        int edgeEnd = 0;

        bool operator<(const SimilarityRepresentativeKey& other) const {
            return std::tie(cardId, widgetClass, extent, edgeStart, edgeEnd) <
                   std::tie(other.cardId, other.widgetClass, other.extent, other.edgeStart, other.edgeEnd);
        }
    };

    std::set<SimilarityRepresentativeKey> seenKeys;
    std::vector<LayoutEditWidgetRegion> widgets;
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::WidgetHover) {
            continue;
        }
        const auto& widget = std::get<LayoutEditWidgetRegion>(region.payload);
        if (!widget.supportsSimilarityIndicator) {
            continue;
        }
        const int extent = WidgetExtentForAxis(widget, axis);
        if (extent <= 0) {
            continue;
        }
        SimilarityRepresentativeKey key;
        key.cardId = widget.widget.renderCardId;
        key.widgetClass = widget.widgetClass;
        key.extent = extent;
        if (axis == LayoutGuideAxis::Vertical) {
            key.edgeStart = widget.rect.left;
            key.edgeEnd = widget.rect.right;
        } else {
            key.edgeStart = widget.rect.top;
            key.edgeEnd = widget.rect.bottom;
        }
        if (!seenKeys.insert(std::move(key)).second) {
            continue;
        }
        widgets.push_back(widget);
    }
    return widgets;
}

}  // namespace

std::optional<LayoutEditGuide> HitTestLayoutGuide(const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind == LayoutEditActiveRegionKind::LayoutWeightGuide && region.box.Contains(clientPoint)) {
            return std::get<LayoutEditGuide>(region.payload);
        }
    }
    return std::nullopt;
}

std::optional<LayoutEditWidgetGuide> HitTestWidgetEditGuide(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditWidgetGuide* bestGuide = nullptr;
    int bestPriority = (std::numeric_limits<int>::max)();
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::WidgetGuide || !region.box.Contains(clientPoint)) {
            continue;
        }
        const auto& guide = std::get<LayoutEditWidgetGuide>(region.payload);
        const int priority = GetLayoutEditParameterHitPriority(guide.parameter);
        if (bestGuide == nullptr || priority < bestPriority) {
            bestGuide = &guide;
            bestPriority = priority;
        }
    }
    return bestGuide != nullptr ? std::optional<LayoutEditWidgetGuide>(*bestGuide) : std::nullopt;
}

std::optional<LayoutEditGapAnchor> HitTestGapEditAnchor(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditGapAnchor* bestAnchor = nullptr;
    int bestPriority = (std::numeric_limits<int>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (it->kind != LayoutEditActiveRegionKind::GapHandle || !it->box.Contains(clientPoint)) {
            continue;
        }
        const auto& anchor = std::get<LayoutEditGapAnchor>(it->payload);
        const int priority = GetLayoutEditParameterHitPriority(anchor.key.parameter);
        if (bestAnchor == nullptr || priority < bestPriority) {
            bestAnchor = &anchor;
            bestPriority = priority;
        }
    }
    return bestAnchor != nullptr ? std::optional<LayoutEditGapAnchor>(*bestAnchor) : std::nullopt;
}

std::optional<LayoutEditAnchorRegion> HitTestEditableAnchorTarget(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditAnchorRegion* bestRegion = nullptr;
    long long bestArea = (std::numeric_limits<long long>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!IsAnchorTargetKind(it->kind)) {
            continue;
        }
        const auto& anchor = std::get<LayoutEditAnchorRegion>(it->payload);
        if (!anchor.targetRect.Contains(clientPoint)) {
            continue;
        }
        const long long area = RectArea(anchor.targetRect);
        if (bestRegion == nullptr || area < bestArea) {
            bestRegion = &anchor;
            bestArea = area;
        }
    }
    return bestRegion != nullptr ? std::optional<LayoutEditAnchorRegion>(*bestRegion) : std::nullopt;
}

std::optional<LayoutEditAnchorRegion> HitTestEditableAnchorHandle(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditAnchorRegion* bestRegion = nullptr;
    int bestPriority = 0;
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!IsAnchorHandleKind(it->kind)) {
            continue;
        }
        const auto& anchor = std::get<LayoutEditAnchorRegion>(it->payload);
        if (!AnchorHandleContains(anchor, clientPoint)) {
            continue;
        }
        const int priority = AnchorHoverPriority(anchor);
        if (bestRegion == nullptr || priority < bestPriority) {
            bestRegion = &anchor;
            bestPriority = priority;
        }
    }
    return bestRegion != nullptr ? std::optional<LayoutEditAnchorRegion>(*bestRegion) : std::nullopt;
}

std::optional<LayoutEditColorRegion> HitTestEditableColorRegion(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditColorRegion* bestRegion = nullptr;
    int bestPriority = (std::numeric_limits<int>::max)();
    long long bestArea = (std::numeric_limits<long long>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!IsColorTargetKind(it->kind)) {
            continue;
        }
        const auto& color = std::get<LayoutEditColorRegion>(it->payload);
        if (!color.targetRect.Contains(clientPoint)) {
            continue;
        }
        const int priority = GetLayoutEditParameterHitPriority(color.parameter);
        const long long area = RectArea(color.targetRect);
        if (bestRegion == nullptr || priority < bestPriority || (priority == bestPriority && area < bestArea)) {
            bestRegion = &color;
            bestPriority = priority;
            bestArea = area;
        }
    }
    return bestRegion != nullptr ? std::optional<LayoutEditColorRegion>(*bestRegion) : std::nullopt;
}

std::optional<LayoutEditAnchorRegion> FindEditableAnchorRegion(
    const LayoutEditActiveRegions& regions, const LayoutEditAnchorKey& key) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (!IsAnchorHandleKind(region.kind) && !IsAnchorTargetKind(region.kind)) {
            continue;
        }
        const auto& anchor = std::get<LayoutEditAnchorRegion>(region.payload);
        if (MatchesEditableAnchorKey(anchor.key, key)) {
            return anchor;
        }
    }
    return std::nullopt;
}

std::optional<LayoutEditGapAnchor> FindGapEditAnchor(
    const LayoutEditActiveRegions& regions, const LayoutEditGapAnchorKey& key) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::GapHandle) {
            continue;
        }
        const auto& anchor = std::get<LayoutEditGapAnchor>(region.payload);
        if (MatchesGapEditAnchorKey(anchor.key, key)) {
            return anchor;
        }
    }
    return std::nullopt;
}

std::optional<LayoutEditGuide> FindLayoutEditGuide(
    const LayoutEditActiveRegions& regions, const LayoutEditGuide& guide) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::LayoutWeightGuide) {
            continue;
        }
        const auto& candidate = std::get<LayoutEditGuide>(region.payload);
        if (candidate.renderCardId == guide.renderCardId && candidate.editCardId == guide.editCardId &&
            candidate.nodePath == guide.nodePath && candidate.separatorIndex == guide.separatorIndex) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<LayoutEditWidgetGuide> FindWidgetEditGuide(
    const LayoutEditActiveRegions& regions, const LayoutEditWidgetGuide& guide) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::WidgetGuide) {
            continue;
        }
        const auto& candidate = std::get<LayoutEditWidgetGuide>(region.payload);
        if (candidate.parameter == guide.parameter && candidate.guideId == guide.guideId &&
            MatchesWidgetIdentity(candidate.widget, guide.widget)) {
            return candidate;
        }
    }
    return std::nullopt;
}

LayoutEditHoverResolution ResolveLayoutEditHover(const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    LayoutEditHoverResolution resolution;
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind == LayoutEditActiveRegionKind::Card && region.box.Contains(clientPoint)) {
            const auto& card = std::get<LayoutEditCardRegion>(region.payload);
            resolution.hoveredLayoutCard =
                LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
        }
        if (region.kind == LayoutEditActiveRegionKind::CardHeader && region.box.Contains(clientPoint)) {
            const auto& card = std::get<LayoutEditCardRegion>(region.payload);
            resolution.hoveredEditableCard =
                LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
        }
    }

    const auto anchorHandle = HitTestEditableAnchorHandle(regions, clientPoint);
    const auto setHoveredAnchor = [&]() {
        resolution.hoveredEditableAnchor = anchorHandle->key;
        if (anchorHandle->key.widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = anchorHandle->key.widget;
        }
    };
    const auto setActionableAnchor = [&]() {
        setHoveredAnchor();
        if (anchorHandle->draggable) {
            resolution.actionableAnchorHandle = anchorHandle->key;
        }
    };
    const auto gapAnchor = HitTestGapEditAnchor(regions, clientPoint);
    const auto widgetGuide = HitTestWidgetEditGuide(regions, clientPoint);
    if (anchorHandle.has_value() && gapAnchor.has_value()) {
        const int anchorPriority = LayoutEditAnchorHitPriority(anchorHandle->key);
        const int gapPriority = GetLayoutEditParameterHitPriority(gapAnchor->key.parameter);
        if (anchorPriority <= gapPriority) {
            setActionableAnchor();
            return resolution;
        }
        resolution.hoveredGapEditAnchor = gapAnchor->key;
        resolution.hoveredGapEditAnchorRegion = gapAnchor;
        resolution.actionableGapEditAnchor = gapAnchor->key;
        return resolution;
    }
    if (anchorHandle.has_value() && widgetGuide.has_value()) {
        const int anchorPriority = LayoutEditAnchorHitPriority(anchorHandle->key);
        const int guidePriority = GetLayoutEditParameterHitPriority(widgetGuide->parameter);
        if (anchorPriority <= guidePriority) {
            setActionableAnchor();
            return resolution;
        }
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuide = widgetGuide;
        return resolution;
    }
    if (gapAnchor.has_value() && widgetGuide.has_value()) {
        const int gapPriority = GetLayoutEditParameterHitPriority(gapAnchor->key.parameter);
        const int guidePriority = GetLayoutEditParameterHitPriority(widgetGuide->parameter);
        if (gapPriority <= guidePriority) {
            resolution.hoveredGapEditAnchor = gapAnchor->key;
            resolution.hoveredGapEditAnchorRegion = gapAnchor;
            resolution.actionableGapEditAnchor = gapAnchor->key;
            return resolution;
        }
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuide = widgetGuide;
        return resolution;
    }
    if (anchorHandle.has_value()) {
        setActionableAnchor();
        return resolution;
    }
    if (gapAnchor.has_value()) {
        resolution.hoveredGapEditAnchor = gapAnchor->key;
        resolution.hoveredGapEditAnchorRegion = gapAnchor;
        resolution.actionableGapEditAnchor = gapAnchor->key;
        return resolution;
    }
    if (widgetGuide.has_value()) {
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuide = widgetGuide;
        return resolution;
    }
    if (const auto layoutGuide = HitTestLayoutGuide(regions, clientPoint); layoutGuide.has_value()) {
        resolution.hoveredLayoutGuide = layoutGuide;
        return resolution;
    }

    const auto anchorTarget = HitTestEditableAnchorTarget(regions, clientPoint);
    std::optional<LayoutEditWidgetIdentity> hoveredWidget;
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::WidgetHover || !region.box.Contains(clientPoint)) {
            continue;
        }
        hoveredWidget = std::get<LayoutEditWidgetRegion>(region.payload).widget;
        break;
    }
    if (hoveredWidget.has_value()) {
        resolution.hoveredEditableWidget = hoveredWidget;
        if (anchorTarget.has_value() && MatchesWidgetIdentity(anchorTarget->key.widget, *hoveredWidget)) {
            resolution.hoveredEditableAnchor = anchorTarget->key;
            return resolution;
        }
        return resolution;
    }
    if (anchorTarget.has_value()) {
        resolution.hoveredEditableAnchor = anchorTarget->key;
        return resolution;
    }
    return resolution;
}

std::vector<LayoutGuideSnapCandidate> CollectLayoutGuideSnapCandidates(
    const LayoutEditActiveRegions& regions, const LayoutEditGuide& guide) {
    struct SimilarityTypeKey {
        WidgetClass widgetClass = WidgetClass::Unknown;
        int extent = 0;

        bool operator<(const SimilarityTypeKey& other) const {
            if (widgetClass != other.widgetClass) {
                return widgetClass < other.widgetClass;
            }
            return extent < other.extent;
        }
    };

    std::vector<LayoutEditWidgetRegion> allWidgets = CollectSimilarityIndicatorWidgets(regions, guide.axis);
    std::vector<LayoutEditWidgetRegion> affectedWidgets;
    for (const LayoutEditWidgetRegion& widget : allWidgets) {
        if (IsWidgetAffectedByGuide(widget, guide)) {
            affectedWidgets.push_back(widget);
        }
    }

    std::vector<LayoutGuideSnapCandidate> candidates;
    for (const LayoutEditWidgetRegion& affected : affectedWidgets) {
        const int startExtent = WidgetExtentForAxis(affected, guide.axis);
        if (startExtent <= 0) {
            continue;
        }
        std::set<SimilarityTypeKey> seenTargets;
        for (size_t i = 0; i < allWidgets.size(); ++i) {
            const LayoutEditWidgetRegion& target = allWidgets[i];
            if (MatchesWidgetIdentity(target.widget, affected.widget) || target.widgetClass != affected.widgetClass) {
                continue;
            }
            const SimilarityTypeKey typeKey{target.widgetClass, WidgetExtentForAxis(target, guide.axis)};
            if (!seenTargets.insert(typeKey).second) {
                continue;
            }
            candidates.push_back(LayoutGuideSnapCandidate{
                affected.widget,
                typeKey.extent,
                startExtent,
                std::abs(typeKey.extent - startExtent),
                i,
            });
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (left.startDistance != right.startDistance) {
            return left.startDistance < right.startDistance;
        }
        return left.groupOrder < right.groupOrder;
    });
    return candidates;
}
