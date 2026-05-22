#include "layout_edit/layout_edit_hit_test.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

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

bool LayoutGuideSnapCandidateLess(const LayoutGuideSnapCandidate& left, const LayoutGuideSnapCandidate& right) {
    if (left.startDistance != right.startDistance) {
        return left.startDistance < right.startDistance;
    }
    return left.groupOrder < right.groupOrder;
}

void StableSortLayoutGuideSnapCandidates(std::vector<LayoutGuideSnapCandidate>& candidates) {
    // Size: snap candidate lists are tiny; insertion sort avoids std::stable_sort template code.
    for (size_t i = 1; i < candidates.size(); ++i) {
        LayoutGuideSnapCandidate current = std::move(candidates[i]);
        size_t j = i;
        while (j > 0 && LayoutGuideSnapCandidateLess(current, candidates[j - 1])) {
            candidates[j] = std::move(candidates[j - 1]);
            --j;
        }
        candidates[j] = std::move(current);
    }
}

int AnchorHoverPriority(const LayoutEditAnchorRegion& region) {
    if (region.shape == AnchorShape::Wedge) {
        return -3;
    }
    if (region.shape == AnchorShape::Square) {
        return -2;
    }
    return LayoutEditAnchorHitPriority(region.key);
}

bool AnchorHandleContains(const LayoutEditAnchorRegion& region, RenderPoint clientPoint) {
    if (!region.anchorHitRect.Contains(clientPoint) &&
        !region.anchorRect.Inflate(region.anchorHitPadding, region.anchorHitPadding).Contains(clientPoint)) {
        return false;
    }
    if (region.shape != AnchorShape::Circle) {
        return true;
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

bool MatchesSimilarityRepresentative(
    const LayoutEditWidgetRegion& candidate,
    LayoutGuideAxis axis,
    const std::string& cardId,
    WidgetClass widgetClass,
    int extent,
    int edgeStart,
    int edgeEnd) {
    if (candidate.widget.renderCardId != cardId || candidate.widgetClass != widgetClass ||
        WidgetExtentForAxis(candidate, axis) != extent) {
        return false;
    }
    if (axis == LayoutGuideAxis::Vertical) {
        return candidate.rect.left == edgeStart && candidate.rect.right == edgeEnd;
    }
    return candidate.rect.top == edgeStart && candidate.rect.bottom == edgeEnd;
}

bool HasPriorTargetType(
    const std::vector<LayoutEditWidgetRegion>& widgets,
    size_t targetIndex,
    const LayoutEditWidgetIdentity& affectedWidget,
    WidgetClass widgetClass,
    LayoutGuideAxis axis,
    int extent) {
    for (size_t i = 0; i < targetIndex; ++i) {
        const LayoutEditWidgetRegion& candidate = widgets[i];
        if (!MatchesWidgetIdentity(candidate.widget, affectedWidget) && candidate.widgetClass == widgetClass &&
            WidgetExtentForAxis(candidate, axis) == extent) {
            return true;
        }
    }
    return false;
}

std::vector<LayoutEditWidgetRegion> CollectSimilarityIndicatorWidgets(
    const LayoutEditActiveRegions& regions, LayoutGuideAxis axis) {
    // Size: scan the already-small result list; separate seen-key vectors measured larger.
    std::vector<LayoutEditWidgetRegion> widgets;
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::WidgetHover) {
            continue;
        }
        const auto* widget = LayoutEditActiveRegionPayloadAs<LayoutEditWidgetRegion>(region);
        if (widget == nullptr || !widget->supportsSimilarityIndicator) {
            continue;
        }
        const int extent = WidgetExtentForAxis(*widget, axis);
        if (extent <= 0) {
            continue;
        }
        const int edgeStart = axis == LayoutGuideAxis::Vertical ? widget->rect.left : widget->rect.top;
        const int edgeEnd = axis == LayoutGuideAxis::Vertical ? widget->rect.right : widget->rect.bottom;
        const auto duplicate = [&](const LayoutEditWidgetRegion& candidate) {
            return MatchesSimilarityRepresentative(
                candidate, axis, widget->widget.renderCardId, widget->widgetClass, extent, edgeStart, edgeEnd);
        };
        if (std::find_if(widgets.begin(), widgets.end(), duplicate) != widgets.end()) {
            continue;
        }
        widgets.push_back(*widget);
    }
    return widgets;
}

}  // namespace

const LayoutEditGuide* HitTestLayoutGuide(const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind == LayoutEditActiveRegionKind::LayoutWeightGuide && region.box.Contains(clientPoint)) {
            return LayoutEditActiveRegionPayloadAs<LayoutEditGuide>(region);
        }
    }
    return nullptr;
}

const LayoutEditWidgetGuide* HitTestWidgetEditGuide(const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditWidgetGuide* bestGuide = nullptr;
    int bestPriority = (std::numeric_limits<int>::max)();
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::WidgetGuide || !region.box.Contains(clientPoint)) {
            continue;
        }
        const auto* guide = LayoutEditActiveRegionPayloadAs<LayoutEditWidgetGuide>(region);
        if (guide == nullptr) {
            continue;
        }
        const int priority = GetLayoutEditParameterHitPriority(guide->parameter);
        if (bestGuide == nullptr || priority < bestPriority) {
            bestGuide = guide;
            bestPriority = priority;
        }
    }
    return bestGuide;
}

const LayoutEditGapAnchor* HitTestGapEditAnchor(const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditGapAnchor* bestAnchor = nullptr;
    int bestPriority = (std::numeric_limits<int>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (it->kind != LayoutEditActiveRegionKind::GapHandle || !it->box.Contains(clientPoint)) {
            continue;
        }
        const auto* anchor = LayoutEditActiveRegionPayloadAs<LayoutEditGapAnchor>(*it);
        if (anchor == nullptr) {
            continue;
        }
        const int priority = GetLayoutEditParameterHitPriority(anchor->key.parameter);
        if (bestAnchor == nullptr || priority < bestPriority) {
            bestAnchor = anchor;
            bestPriority = priority;
        }
    }
    return bestAnchor;
}

const LayoutEditAnchorRegion* HitTestEditableAnchorTarget(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditAnchorRegion* bestRegion = nullptr;
    long long bestArea = (std::numeric_limits<long long>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!IsAnchorTargetKind(it->kind)) {
            continue;
        }
        const auto* anchor = LayoutEditActiveRegionPayloadAs<LayoutEditAnchorRegion>(*it);
        if (anchor == nullptr || !anchor->targetRect.Contains(clientPoint)) {
            continue;
        }
        const long long area = RectArea(anchor->targetRect);
        if (bestRegion == nullptr || area < bestArea) {
            bestRegion = anchor;
            bestArea = area;
        }
    }
    return bestRegion;
}

const LayoutEditAnchorRegion* HitTestEditableAnchorHandle(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditAnchorRegion* bestRegion = nullptr;
    int bestPriority = 0;
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!IsAnchorHandleKind(it->kind)) {
            continue;
        }
        const auto* anchor = LayoutEditActiveRegionPayloadAs<LayoutEditAnchorRegion>(*it);
        if (anchor == nullptr || !AnchorHandleContains(*anchor, clientPoint)) {
            continue;
        }
        const int priority = AnchorHoverPriority(*anchor);
        if (bestRegion == nullptr || priority < bestPriority) {
            bestRegion = anchor;
            bestPriority = priority;
        }
    }
    return bestRegion;
}

const LayoutEditColorRegion* HitTestEditableColorRegion(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    const LayoutEditColorRegion* bestRegion = nullptr;
    int bestPriority = (std::numeric_limits<int>::max)();
    long long bestArea = (std::numeric_limits<long long>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!IsColorTargetKind(it->kind)) {
            continue;
        }
        const auto* color = LayoutEditActiveRegionPayloadAs<LayoutEditColorRegion>(*it);
        if (color == nullptr || !color->targetRect.Contains(clientPoint)) {
            continue;
        }
        const int priority = GetLayoutEditParameterHitPriority(color->parameter);
        const long long area = RectArea(color->targetRect);
        if (bestRegion == nullptr || priority < bestPriority || (priority == bestPriority && area < bestArea)) {
            bestRegion = color;
            bestPriority = priority;
            bestArea = area;
        }
    }
    return bestRegion;
}

const LayoutEditAnchorRegion* FindEditableAnchorRegion(
    const LayoutEditActiveRegions& regions, const LayoutEditAnchorKey& key) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (!IsAnchorHandleKind(region.kind) && !IsAnchorTargetKind(region.kind)) {
            continue;
        }
        const auto* anchor = LayoutEditActiveRegionPayloadAs<LayoutEditAnchorRegion>(region);
        if (anchor != nullptr && MatchesEditableAnchorKey(anchor->key, key)) {
            return anchor;
        }
    }
    return nullptr;
}

const LayoutEditGapAnchor* FindGapEditAnchor(
    const LayoutEditActiveRegions& regions, const LayoutEditGapAnchorKey& key) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::GapHandle) {
            continue;
        }
        const auto* anchor = LayoutEditActiveRegionPayloadAs<LayoutEditGapAnchor>(region);
        if (anchor != nullptr && MatchesGapEditAnchorKey(anchor->key, key)) {
            return anchor;
        }
    }
    return nullptr;
}

const LayoutEditGuide* FindLayoutEditGuide(const LayoutEditActiveRegions& regions, const LayoutEditGuide& guide) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::LayoutWeightGuide) {
            continue;
        }
        const auto* candidate = LayoutEditActiveRegionPayloadAs<LayoutEditGuide>(region);
        if (candidate != nullptr && candidate->renderCardId == guide.renderCardId &&
            candidate->editCardId == guide.editCardId && candidate->nodePath == guide.nodePath &&
            candidate->separatorIndex == guide.separatorIndex) {
            return candidate;
        }
    }
    return nullptr;
}

const LayoutEditWidgetGuide* FindWidgetEditGuide(
    const LayoutEditActiveRegions& regions, const LayoutEditWidgetGuide& guide) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::WidgetGuide) {
            continue;
        }
        const auto* candidate = LayoutEditActiveRegionPayloadAs<LayoutEditWidgetGuide>(region);
        if (candidate != nullptr && candidate->parameter == guide.parameter && candidate->guideId == guide.guideId &&
            MatchesWidgetIdentity(candidate->widget, guide.widget)) {
            return candidate;
        }
    }
    return nullptr;
}

LayoutEditHoverResolution ResolveLayoutEditHover(const LayoutEditActiveRegions& regions, RenderPoint clientPoint) {
    LayoutEditHoverResolution resolution;
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind == LayoutEditActiveRegionKind::Card && region.box.Contains(clientPoint)) {
            if (const auto* card = LayoutEditActiveRegionPayloadAs<LayoutEditCardRegion>(region)) {
                resolution.hoveredLayoutCard =
                    LayoutEditWidgetIdentity{card->id, card->id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
            }
        }
        if (region.kind == LayoutEditActiveRegionKind::CardHeader && region.box.Contains(clientPoint)) {
            if (const auto* card = LayoutEditActiveRegionPayloadAs<LayoutEditCardRegion>(region)) {
                resolution.hoveredEditableCard =
                    LayoutEditWidgetIdentity{card->id, card->id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
            }
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
    if (anchorHandle != nullptr && gapAnchor != nullptr) {
        const int anchorPriority = LayoutEditAnchorHitPriority(anchorHandle->key);
        const int gapPriority = GetLayoutEditParameterHitPriority(gapAnchor->key.parameter);
        if (anchorPriority <= gapPriority) {
            setActionableAnchor();
            return resolution;
        }
        resolution.hoveredGapEditAnchor = gapAnchor->key;
        resolution.hoveredGapEditAnchorRegion = *gapAnchor;
        resolution.actionableGapEditAnchor = gapAnchor->key;
        return resolution;
    }
    if (anchorHandle != nullptr && widgetGuide != nullptr) {
        const int anchorPriority = LayoutEditAnchorHitPriority(anchorHandle->key);
        const int guidePriority = GetLayoutEditParameterHitPriority(widgetGuide->parameter);
        if (anchorPriority <= guidePriority) {
            setActionableAnchor();
            return resolution;
        }
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuide = *widgetGuide;
        return resolution;
    }
    if (gapAnchor != nullptr && widgetGuide != nullptr) {
        const int gapPriority = GetLayoutEditParameterHitPriority(gapAnchor->key.parameter);
        const int guidePriority = GetLayoutEditParameterHitPriority(widgetGuide->parameter);
        if (gapPriority <= guidePriority) {
            resolution.hoveredGapEditAnchor = gapAnchor->key;
            resolution.hoveredGapEditAnchorRegion = *gapAnchor;
            resolution.actionableGapEditAnchor = gapAnchor->key;
            return resolution;
        }
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuide = *widgetGuide;
        return resolution;
    }
    if (anchorHandle != nullptr) {
        setActionableAnchor();
        return resolution;
    }
    if (gapAnchor != nullptr) {
        resolution.hoveredGapEditAnchor = gapAnchor->key;
        resolution.hoveredGapEditAnchorRegion = *gapAnchor;
        resolution.actionableGapEditAnchor = gapAnchor->key;
        return resolution;
    }
    if (widgetGuide != nullptr) {
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuide = *widgetGuide;
        return resolution;
    }
    if (const LayoutEditGuide* layoutGuide = HitTestLayoutGuide(regions, clientPoint); layoutGuide != nullptr) {
        resolution.hoveredLayoutGuide = *layoutGuide;
        return resolution;
    }

    const auto anchorTarget = HitTestEditableAnchorTarget(regions, clientPoint);
    std::optional<LayoutEditWidgetIdentity> hoveredWidget;
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::WidgetHover || !region.box.Contains(clientPoint)) {
            continue;
        }
        if (const auto* widget = LayoutEditActiveRegionPayloadAs<LayoutEditWidgetRegion>(region)) {
            hoveredWidget = widget->widget;
        }
        break;
    }
    if (hoveredWidget.has_value()) {
        resolution.hoveredEditableWidget = hoveredWidget;
        if (anchorTarget != nullptr && MatchesWidgetIdentity(anchorTarget->key.widget, *hoveredWidget)) {
            resolution.hoveredEditableAnchor = anchorTarget->key;
            return resolution;
        }
        return resolution;
    }
    if (anchorTarget != nullptr) {
        resolution.hoveredEditableAnchor = anchorTarget->key;
        return resolution;
    }
    return resolution;
}

std::vector<LayoutGuideSnapCandidate> CollectLayoutGuideSnapCandidates(
    const LayoutEditActiveRegions& regions, const LayoutEditGuide& guide) {
    std::vector<LayoutEditWidgetRegion> allWidgets = CollectSimilarityIndicatorWidgets(regions, guide.axis);
    std::vector<LayoutGuideSnapCandidate> candidates;
    for (const LayoutEditWidgetRegion& affected : allWidgets) {
        if (!IsWidgetAffectedByGuide(affected, guide)) {
            continue;
        }
        const int startExtent = WidgetExtentForAxis(affected, guide.axis);
        if (startExtent <= 0) {
            continue;
        }
        for (size_t i = 0; i < allWidgets.size(); ++i) {
            const LayoutEditWidgetRegion& target = allWidgets[i];
            if (MatchesWidgetIdentity(target.widget, affected.widget) || target.widgetClass != affected.widgetClass) {
                continue;
            }
            const int targetExtent = WidgetExtentForAxis(target, guide.axis);
            if (HasPriorTargetType(allWidgets, i, affected.widget, target.widgetClass, guide.axis, targetExtent)) {
                continue;
            }
            candidates.push_back(
                LayoutGuideSnapCandidate{
                    affected.widget,
                    targetExtent,
                    startExtent,
                    std::abs(targetExtent - startExtent),
                    i,
                });
        }
    }

    StableSortLayoutGuideSnapCandidates(candidates);
    return candidates;
}
