#include "layout_guide_sheet/impl/layout_guide_sheet_placement.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

#include "util/resource_strings.h"
#include "util/text_format.h"

namespace {

long long Cross(RenderPoint a, RenderPoint b, RenderPoint c) {
    return static_cast<long long>(b.x - a.x) * static_cast<long long>(c.y - a.y) -
        static_cast<long long>(b.y - a.y) * static_cast<long long>(c.x - a.x);
}

bool PointsEqual(RenderPoint lhs, RenderPoint rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

int Min3Int(int first, int second, int third) {
    // Size: avoid std::min/std::max initializer_list helper code in guide-sheet layout.
    return std::min(std::min(first, second), third);
}

int Max3Int(int first, int second, int third) {
    return std::max(std::max(first, second), third);
}

bool LeaderSegmentsIntersect(RenderPoint a, RenderPoint b, RenderPoint c, RenderPoint d) {
    if (PointsEqual(a, c) || PointsEqual(a, d) || PointsEqual(b, c) || PointsEqual(b, d)) {
        return false;
    }
    const long long abC = Cross(a, b, c);
    const long long abD = Cross(a, b, d);
    const long long cdA = Cross(c, d, a);
    const long long cdB = Cross(c, d, b);
    return ((abC > 0 && abD < 0) || (abC < 0 && abD > 0)) && ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0));
}

bool SegmentIntersectsRect(RenderPoint a, RenderPoint b, const RenderRect& rect) {
    if (rect.IsEmpty()) {
        return false;
    }
    const int segmentLeft = std::min(a.x, b.x);
    const int segmentRight = std::max(a.x, b.x);
    const int segmentTop = std::min(a.y, b.y);
    const int segmentBottom = std::max(a.y, b.y);
    if (segmentRight < rect.left || segmentLeft > rect.right || segmentBottom < rect.top || segmentTop > rect.bottom) {
        return false;
    }
    if (rect.Contains(a) || rect.Contains(b)) {
        return true;
    }
    const RenderPoint topLeft{rect.left, rect.top};
    const RenderPoint topRight{rect.right, rect.top};
    const RenderPoint bottomLeft{rect.left, rect.bottom};
    const RenderPoint bottomRight{rect.right, rect.bottom};
    return LeaderSegmentsIntersect(a, b, topLeft, topRight) || LeaderSegmentsIntersect(a, b, topRight, bottomRight) ||
        LeaderSegmentsIntersect(a, b, bottomRight, bottomLeft) || LeaderSegmentsIntersect(a, b, bottomLeft, topLeft);
}

RenderRect TargetSafeRect(RenderPoint target, int radius) {
    return RenderRect{target.x - radius, target.y - radius, target.x + radius + 1, target.y + radius + 1};
}

const char* ExitSideName(LayoutGuideSheetExitSide side) {
    switch (side) {
        case LayoutGuideSheetExitSide::Left:
            return "left";
        case LayoutGuideSheetExitSide::Right:
            return "right";
        case LayoutGuideSheetExitSide::Top:
            return "top";
        case LayoutGuideSheetExitSide::Bottom:
            return "bottom";
    }
    return "right";
}

RenderPoint TransformPoint(RenderPoint point, const RenderRect& source, const RenderRect& dest) {
    const double scaleX = source.Width() == 0 ? 1.0 : static_cast<double>(dest.Width()) / source.Width();
    const double scaleY = source.Height() == 0 ? 1.0 : static_cast<double>(dest.Height()) / source.Height();
    return RenderPoint{dest.left + static_cast<int>((point.x - source.left) * scaleX + 0.5),
        dest.top + static_cast<int>((point.y - source.top) * scaleY + 0.5)};
}

RenderRect TransformRect(const RenderRect& rect, const RenderRect& source, const RenderRect& dest) {
    const RenderPoint topLeft = TransformPoint(RenderPoint{rect.left, rect.top}, source, dest);
    const RenderPoint bottomRight = TransformPoint(RenderPoint{rect.right, rect.bottom}, source, dest);
    return RenderRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
}

RenderRect OffsetRenderRect(RenderRect rect, int dx, int dy) {
    rect.left += dx;
    rect.right += dx;
    rect.top += dy;
    rect.bottom += dy;
    return rect;
}

RenderPoint ClosestEllipseBoundaryPoint(const RenderRect& rect, RenderPoint reference) {
    const RenderPoint center = rect.Center();
    const double radiusX = static_cast<double>(std::max(1, rect.Width())) / 2.0;
    const double radiusY = static_cast<double>(std::max(1, rect.Height())) / 2.0;
    const double dx = static_cast<double>(reference.x - center.x);
    const double dy = static_cast<double>(reference.y - center.y);
    const double normalizedLength = std::sqrt((dx * dx) / (radiusX * radiusX) + (dy * dy) / (radiusY * radiusY));
    if (normalizedLength <= 0.0) {
        return RenderPoint{center.x, rect.top};
    }
    return RenderPoint{center.x + static_cast<int>(std::lround(dx / normalizedLength)),
        center.y + static_cast<int>(std::lround(dy / normalizedLength))};
}

bool LooksLikeGaugeHalfRingRect(const RenderRect& rect) {
    if (rect.Width() <= 0 || rect.Height() <= 0) {
        return false;
    }
    const int expectedHeight = rect.Width() * 2;
    return std::abs(rect.Height() - expectedHeight) <= std::max(2, rect.Height() / 8);
}

std::optional<RenderPoint> GaugeRingColorAttachmentPoint(
    const RenderRect& rect, std::optional<LayoutEditParameter> parameter, int ringThickness) {
    if (!parameter.has_value() || !LooksLikeGaugeHalfRingRect(rect)) {
        return std::nullopt;
    }
    if (*parameter != LayoutEditParameter::ColorAccent && *parameter != LayoutEditParameter::ColorTrack) {
        return std::nullopt;
    }

    const int outerRadius = std::min(rect.Width(), std::max(1, rect.Height() / 2));
    const int ringCenterRadius = std::max(0, outerRadius - std::max(1, ringThickness) / 2);
    const int centerY = rect.Center().y;
    if (*parameter == LayoutEditParameter::ColorAccent) {
        return RenderPoint{rect.right - ringCenterRadius, centerY};
    }
    return RenderPoint{rect.left + ringCenterRadius, centerY};
}

struct PlannedCallout {
    size_t calloutIndex = 0;
    size_t cardIndex = 0;
    RenderRect target{};
};

struct CardCalloutColumns {
    std::vector<size_t> top;
    std::vector<size_t> left;
    std::vector<size_t> right;
    std::vector<size_t> bottom;
    int leaderRepairPasses = 0;
};

struct BlockLayout {
    int width = 0;
    int height = 0;
    int advanceHeight = 0;
    int itemWidth = 0;
    int itemHeight = 0;
    int itemX = 0;
    int itemY = 0;
    int leftWidth = 0;
    int rightWidth = 0;
};

struct TrialLeader {
    size_t plannedIndex = 0;
    RenderPoint target{};
    RenderPoint bubble{};
    RenderRect targetSafeRect{};
};

struct LeaderConflict {
    size_t first = 0;
    size_t second = 0;
};

struct LeaderScore {
    int score = 0;
    std::vector<LeaderConflict> conflicts;
};

inline constexpr size_t kMaxAdjacentOrderPasses = 20;
inline constexpr int kLeaderCrossingScore = 100;
inline constexpr int kTargetSafeZoneScore = 1;

RenderPoint TargetAttachmentForCallout(const LayoutGuideSheetPlacementCallout& callout,
    const RenderRect& targetRect,
    RenderPoint bubbleAttachment,
    int gaugeRingThickness) {
    const std::optional<RenderPoint> gaugeColorAttachment =
        GaugeRingColorAttachmentPoint(targetRect, callout.hoverColorParameter, gaugeRingThickness);
    if (gaugeColorAttachment.has_value()) {
        return *gaugeColorAttachment;
    }
    if (callout.targetAttachmentOnAnchorCircle) {
        return ClosestEllipseBoundaryPoint(targetRect, bubbleAttachment);
    }
    if (callout.hoverWidgetGuide.has_value() &&
        callout.hoverWidgetGuide->parameter == LayoutEditParameter::ThroughputAxisPadding) {
        return RenderPoint{targetRect.Center().x, targetRect.top + std::max(0, targetRect.Height()) / 4};
    }
    return targetRect.Center();
}

bool RectsOverlap(const RenderRect& lhs, const RenderRect& rhs) {
    return !lhs.IsEmpty() && !rhs.IsEmpty() && lhs.left < rhs.right && lhs.right > rhs.left && lhs.top < rhs.bottom &&
        lhs.bottom > rhs.top;
}

int OrderPenalty(const std::vector<size_t>& indexes, const std::vector<size_t>& preferredOrder) {
    int penalty = 0;
    for (size_t i = 0; i < indexes.size(); ++i) {
        const auto preferredIt = std::find(preferredOrder.begin(), preferredOrder.end(), indexes[i]);
        if (preferredIt == preferredOrder.end()) {
            continue;
        }
        const int preferredPosition = static_cast<int>(preferredIt - preferredOrder.begin());
        penalty += std::abs(static_cast<int>(i) - preferredPosition);
    }
    return penalty;
}

int SideMembershipPenalty(const CardCalloutColumns& candidate, const CardCalloutColumns& preferred) {
    int penalty = 0;
    for (const size_t index : candidate.left) {
        if (std::find(preferred.left.begin(), preferred.left.end(), index) == preferred.left.end()) {
            ++penalty;
        }
    }
    for (const size_t index : candidate.right) {
        if (std::find(preferred.right.begin(), preferred.right.end(), index) == preferred.right.end()) {
            ++penalty;
        }
    }
    return penalty;
}

bool PlannedIndexLessByTargetX(size_t lhs,
    size_t rhs,
    const std::vector<PlannedCallout>& plannedCallouts,
    const std::vector<LayoutGuideSheetPlacementCallout>& callouts) {
    const RenderPoint lhsCenter = plannedCallouts[lhs].target.Center();
    const RenderPoint rhsCenter = plannedCallouts[rhs].target.Center();
    if (lhsCenter.x != rhsCenter.x) {
        return lhsCenter.x < rhsCenter.x;
    }
    if (lhsCenter.y != rhsCenter.y) {
        return lhsCenter.y < rhsCenter.y;
    }
    return callouts[plannedCallouts[lhs].calloutIndex].order < callouts[plannedCallouts[rhs].calloutIndex].order;
}

bool PlannedIndexLessByTargetY(size_t lhs,
    size_t rhs,
    const std::vector<PlannedCallout>& plannedCallouts,
    const std::vector<LayoutGuideSheetPlacementCallout>& callouts) {
    const RenderPoint lhsCenter = plannedCallouts[lhs].target.Center();
    const RenderPoint rhsCenter = plannedCallouts[rhs].target.Center();
    if (lhsCenter.y != rhsCenter.y) {
        return lhsCenter.y < rhsCenter.y;
    }
    if (lhsCenter.x != rhsCenter.x) {
        return lhsCenter.x < rhsCenter.x;
    }
    return callouts[plannedCallouts[lhs].calloutIndex].order < callouts[plannedCallouts[rhs].calloutIndex].order;
}

void StableSortPlannedIndexesByTargetX(std::vector<size_t>& plannedIndexes,
    const std::vector<PlannedCallout>& plannedCallouts,
    const std::vector<LayoutGuideSheetPlacementCallout>& callouts) {
    // Size: callout lists are small; insertion sort avoids std::stable_sort template code.
    for (size_t i = 1; i < plannedIndexes.size(); ++i) {
        const size_t current = plannedIndexes[i];
        size_t j = i;
        while (j > 0 && PlannedIndexLessByTargetX(current, plannedIndexes[j - 1], plannedCallouts, callouts)) {
            plannedIndexes[j] = plannedIndexes[j - 1];
            --j;
        }
        plannedIndexes[j] = current;
    }
}

void StableSortPlannedIndexesByTargetY(std::vector<size_t>& plannedIndexes,
    const std::vector<PlannedCallout>& plannedCallouts,
    const std::vector<LayoutGuideSheetPlacementCallout>& callouts) {
    for (size_t i = 1; i < plannedIndexes.size(); ++i) {
        const size_t current = plannedIndexes[i];
        size_t j = i;
        while (j > 0 && PlannedIndexLessByTargetY(current, plannedIndexes[j - 1], plannedCallouts, callouts)) {
            plannedIndexes[j] = plannedIndexes[j - 1];
            --j;
        }
        plannedIndexes[j] = current;
    }
}

}  // namespace

LayoutGuideSheetPlacementResult PlaceLayoutGuideSheetCallouts(
    std::vector<LayoutGuideSheetCardPlacement>& cardPlacements,
    std::vector<LayoutGuideSheetPlacementCallout>& callouts,
    const LayoutGuideSheetPlacementStyle& style,
    const LayoutGuideSheetConstrainCalloutWidth& constrainCalloutWidth,
    std::vector<std::string>* traceDetails) {
    LayoutGuideSheetPlacementResult result;
    std::vector<PlannedCallout> plannedCallouts;
    plannedCallouts.reserve(callouts.size());
    const auto cardOrder = [&](const std::string& cardId) {
        const auto it = std::find_if(cardPlacements.begin(), cardPlacements.end(), [&](const auto& placement) {
            return placement.id == cardId;
        });
        if (it != cardPlacements.end()) {
            return static_cast<size_t>(it - cardPlacements.begin());
        }
        return cardPlacements.size();
    };
    for (size_t i = 0; i < callouts.size(); ++i) {
        const LayoutGuideSheetPlacementCallout& callout = callouts[i];
        const size_t calloutCardOrder = cardOrder(callout.sourceCardId);
        if (calloutCardOrder >= cardPlacements.size()) {
            continue;
        }
        plannedCallouts.push_back(PlannedCallout{i, calloutCardOrder, callout.targetRect});
    }

    std::vector<CardCalloutColumns> plannedByCard(cardPlacements.size());
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        std::vector<size_t> cardPlanned;
        for (size_t plannedIndex = 0; plannedIndex < plannedCallouts.size(); ++plannedIndex) {
            if (plannedCallouts[plannedIndex].cardIndex == cardIndex) {
                cardPlanned.push_back(plannedIndex);
            }
        }
        StableSortPlannedIndexesByTargetX(cardPlanned, plannedCallouts, callouts);
        const size_t leftCount = cardPlanned.size() == 1 ?
            (plannedCallouts[cardPlanned.front()].target.Center().x < cardPlacements[cardIndex].sourceRect.Center().x ?
                    1 :
                    0) :
            cardPlanned.size() / 2;
        plannedByCard[cardIndex].left.assign(cardPlanned.begin(), cardPlanned.begin() + leftCount);
        plannedByCard[cardIndex].right.assign(cardPlanned.begin() + leftCount, cardPlanned.end());
        StableSortPlannedIndexesByTargetY(plannedByCard[cardIndex].left, plannedCallouts, callouts);
        StableSortPlannedIndexesByTargetY(plannedByCard[cardIndex].right, plannedCallouts, callouts);
    }

    const auto stackedHeight = [&](const std::vector<size_t>& plannedIndexes) {
        int height = 0;
        for (size_t i = 0; i < plannedIndexes.size(); ++i) {
            height += callouts[plannedCallouts[plannedIndexes[i]].calloutIndex].bubbleRect.Height();
            if (i + 1 < plannedIndexes.size()) {
                height += style.rowGap;
            }
        }
        return height;
    };

    const auto widestBubbleWidthFor = [&](const std::vector<size_t>& plannedIndexes) {
        int width = 0;
        for (const size_t plannedIndex : plannedIndexes) {
            width = std::max(width, callouts[plannedCallouts[plannedIndex].calloutIndex].bubbleRect.Width());
        }
        return width;
    };

    const auto computeBlockForColumns =
        [&](const CardCalloutColumns& columns, const LayoutGuideSheetCardPlacement& placement) {
            BlockLayout block;
            block.itemHeight = placement.sourceRect.Height();
            block.itemWidth = placement.sourceRect.Width();
            block.leftWidth = widestBubbleWidthFor(columns.left);
            block.rightWidth = widestBubbleWidthFor(columns.right);
            const int topWidth = widestBubbleWidthFor(columns.top);
            const int bottomWidth = widestBubbleWidthFor(columns.bottom);
            const int topHeight = stackedHeight(columns.top);
            const int bottomHeight = stackedHeight(columns.bottom);
            const int topProtrusion = topHeight > 0 ? topHeight + style.calloutGap : 0;
            const int bottomProtrusion = bottomHeight > 0 ? bottomHeight + style.calloutGap : 0;
            block.itemX = block.leftWidth > 0 ? block.leftWidth + style.calloutGap : 0;
            const int sideStackHeight = std::max(stackedHeight(columns.left), stackedHeight(columns.right));
            const int sideAbove = std::max(0, (sideStackHeight - block.itemHeight) / 2);
            const int sideBelow = std::max(0, sideStackHeight - block.itemHeight - sideAbove);
            block.itemY = std::max(topProtrusion, sideAbove);
            block.height = block.itemY + block.itemHeight + std::max(bottomProtrusion, sideBelow);
            block.advanceHeight = block.height;
            const int mainWidth =
                block.itemX + block.itemWidth + (block.rightWidth > 0 ? style.calloutGap + block.rightWidth : 0);
            int topX = block.itemX + (block.itemWidth - topWidth) / 2;
            int bottomX = block.itemX + (block.itemWidth - bottomWidth) / 2;
            const int minX = Min3Int(0, topX, bottomX);
            const int maxX = Max3Int(mainWidth, topX + topWidth, bottomX + bottomWidth);
            block.itemX -= minX;
            block.width = maxX - minX;
            return block;
        };

    const auto appendTrialLeaders =
        [&](std::vector<TrialLeader>& leaders,
            const std::vector<size_t>& plannedIndexes,
            LayoutGuideSheetExitSide side,
            const LayoutGuideSheetCardPlacement& placement,
            const BlockLayout& block) {
            const RenderRect cardRect{
                block.itemX, block.itemY, block.itemX + block.itemWidth, block.itemY + block.itemHeight};
            int y = cardRect.Center().y - stackedHeight(plannedIndexes) / 2;
            for (const size_t plannedIndex : plannedIndexes) {
                const PlannedCallout& planned = plannedCallouts[plannedIndex];
                const LayoutGuideSheetPlacementCallout& callout = callouts[planned.calloutIndex];
                const int bubbleX = side == LayoutGuideSheetExitSide::Left ?
                    block.itemX - style.calloutGap - callout.bubbleRect.Width() :
                    block.itemX + block.itemWidth + style.calloutGap;
                const RenderRect bubbleRect{
                    bubbleX, y, bubbleX + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
                const RenderPoint bubbleAttachment{
                    side == LayoutGuideSheetExitSide::Left ? bubbleRect.right : bubbleRect.left, bubbleRect.Center().y};
                const RenderRect targetRect = placement.overview ?
                    TransformRect(planned.target, placement.sourceRect, cardRect) :
                    OffsetRenderRect(planned.target,
                        cardRect.left - placement.sourceRect.left,
                        cardRect.top - placement.sourceRect.top);
                const RenderPoint targetAttachment =
                    TargetAttachmentForCallout(callout, targetRect, bubbleAttachment, style.gaugeRingThickness);
                leaders.push_back(TrialLeader{plannedIndex,
                    targetAttachment,
                    bubbleAttachment,
                    TargetSafeRect(targetAttachment, style.targetSafeRadius)});
                y = bubbleRect.bottom + style.rowGap;
            }
        };

    const auto appendTopBottomTrialLeaders =
        [&](std::vector<TrialLeader>& leaders,
            const std::vector<size_t>& plannedIndexes,
            LayoutGuideSheetExitSide side,
            const LayoutGuideSheetCardPlacement& placement,
            const BlockLayout& block) {
            const RenderRect cardRect{
                block.itemX, block.itemY, block.itemX + block.itemWidth, block.itemY + block.itemHeight};
            for (const size_t plannedIndex : plannedIndexes) {
                const PlannedCallout& planned = plannedCallouts[plannedIndex];
                const LayoutGuideSheetPlacementCallout& callout = callouts[planned.calloutIndex];
                const int bubbleX = block.itemX + (block.itemWidth - callout.bubbleRect.Width()) / 2;
                const int bubbleY = side == LayoutGuideSheetExitSide::Top ?
                    block.itemY - style.calloutGap - callout.bubbleRect.Height() :
                    block.itemY + block.itemHeight + style.calloutGap;
                const RenderRect bubbleRect{
                    bubbleX, bubbleY, bubbleX + callout.bubbleRect.Width(), bubbleY + callout.bubbleRect.Height()};
                const RenderPoint bubbleAttachment{
                    bubbleRect.Center().x, side == LayoutGuideSheetExitSide::Top ? bubbleRect.bottom : bubbleRect.top};
                const RenderRect targetRect = placement.overview ?
                    TransformRect(planned.target, placement.sourceRect, cardRect) :
                    OffsetRenderRect(planned.target,
                        cardRect.left - placement.sourceRect.left,
                        cardRect.top - placement.sourceRect.top);
                const RenderPoint targetAttachment =
                    TargetAttachmentForCallout(callout, targetRect, bubbleAttachment, style.gaugeRingThickness);
                leaders.push_back(TrialLeader{plannedIndex,
                    targetAttachment,
                    bubbleAttachment,
                    TargetSafeRect(targetAttachment, style.targetSafeRadius)});
            }
        };

    const auto collectLeaderScore =
        [&](const CardCalloutColumns& columns,
            const LayoutGuideSheetCardPlacement& placement,
            int stopAfter = (std::numeric_limits<int>::max)()) {
            const BlockLayout block = computeBlockForColumns(columns, placement);
            std::vector<TrialLeader> leaders;
            leaders.reserve(columns.top.size() + columns.left.size() + columns.right.size() + columns.bottom.size());
            appendTopBottomTrialLeaders(leaders, columns.top, LayoutGuideSheetExitSide::Top, placement, block);
            appendTrialLeaders(leaders, columns.left, LayoutGuideSheetExitSide::Left, placement, block);
            appendTrialLeaders(leaders, columns.right, LayoutGuideSheetExitSide::Right, placement, block);
            appendTopBottomTrialLeaders(leaders, columns.bottom, LayoutGuideSheetExitSide::Bottom, placement, block);

            LeaderScore score;
            for (size_t i = 0; i < leaders.size(); ++i) {
                for (size_t j = i + 1; j < leaders.size(); ++j) {
                    if (LeaderSegmentsIntersect(
                            leaders[i].target, leaders[i].bubble, leaders[j].target, leaders[j].bubble)) {
                        score.score += kLeaderCrossingScore;
                        score.conflicts.push_back(LeaderConflict{leaders[i].plannedIndex, leaders[j].plannedIndex});
                        if (score.score > stopAfter) {
                            return score;
                        }
                    }
                    if (SegmentIntersectsRect(leaders[i].target, leaders[i].bubble, leaders[j].targetSafeRect)) {
                        score.score += kTargetSafeZoneScore;
                        score.conflicts.push_back(LeaderConflict{leaders[i].plannedIndex, leaders[j].plannedIndex});
                        if (score.score > stopAfter) {
                            return score;
                        }
                    }
                    if (SegmentIntersectsRect(leaders[j].target, leaders[j].bubble, leaders[i].targetSafeRect)) {
                        score.score += kTargetSafeZoneScore;
                        score.conflicts.push_back(LeaderConflict{leaders[j].plannedIndex, leaders[i].plannedIndex});
                        if (score.score > stopAfter) {
                            return score;
                        }
                    }
                }
            }
            return score;
        };

    const auto countLeaderIntersections =
        [&](const CardCalloutColumns& columns,
            const LayoutGuideSheetCardPlacement& placement,
            int stopAfter = (std::numeric_limits<int>::max)()) {
            return collectLeaderScore(columns, placement, stopAfter).score;
        };

    const auto placementPenalty = [&](const CardCalloutColumns& columns, const CardCalloutColumns& preferred) {
        return SideMembershipPenalty(columns, preferred) + OrderPenalty(columns.left, preferred.left) +
            OrderPenalty(columns.right, preferred.right);
    };

    const auto stackForSide = [](CardCalloutColumns& columns, LayoutGuideSheetExitSide side) -> std::vector<size_t>& {
        switch (side) {
            case LayoutGuideSheetExitSide::Top:
                return columns.top;
            case LayoutGuideSheetExitSide::Left:
                return columns.left;
            case LayoutGuideSheetExitSide::Right:
                return columns.right;
            case LayoutGuideSheetExitSide::Bottom:
                return columns.bottom;
        }
        return columns.right;
    };

    const auto findPlannedIndex = [&](CardCalloutColumns& columns, size_t plannedIndex) {
        struct Location {
            LayoutGuideSheetExitSide side = LayoutGuideSheetExitSide::Right;
            size_t index = 0;
            bool found = false;
        };
        const LayoutGuideSheetExitSide sides[]{LayoutGuideSheetExitSide::Top,
            LayoutGuideSheetExitSide::Left,
            LayoutGuideSheetExitSide::Right,
            LayoutGuideSheetExitSide::Bottom};
        for (const LayoutGuideSheetExitSide side : sides) {
            std::vector<size_t>& stack = stackForSide(columns, side);
            const auto it = std::find(stack.begin(), stack.end(), plannedIndex);
            if (it != stack.end()) {
                return Location{side, static_cast<size_t>(it - stack.begin()), true};
            }
        }
        return Location{};
    };

    const auto swapPlannedIndexes = [&](CardCalloutColumns& columns, size_t lhs, size_t rhs) {
        if (lhs == rhs) {
            return false;
        }
        const auto lhsLocation = findPlannedIndex(columns, lhs);
        const auto rhsLocation = findPlannedIndex(columns, rhs);
        if (!lhsLocation.found || !rhsLocation.found) {
            return false;
        }
        std::vector<size_t>& lhsStack = stackForSide(columns, lhsLocation.side);
        std::vector<size_t>& rhsStack = stackForSide(columns, rhsLocation.side);
        std::swap(lhsStack[lhsLocation.index], rhsStack[rhsLocation.index]);
        return true;
    };

    const auto removePlannedIndex = [&](CardCalloutColumns& columns, size_t plannedIndex) {
        const auto location = findPlannedIndex(columns, plannedIndex);
        if (!location.found) {
            return false;
        }
        std::vector<size_t>& stack = stackForSide(columns, location.side);
        stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(location.index));
        return true;
    };

    const auto promoteInitialTopBottom = [&](CardCalloutColumns& columns) {
        if (!columns.left.empty()) {
            const size_t bottom = columns.left.back();
            columns.left.pop_back();
            columns.bottom.push_back(bottom);
        }
        if (!columns.right.empty()) {
            const size_t top = columns.right.front();
            columns.right.erase(columns.right.begin());
            columns.top.push_back(top);
        }
    };

    const auto optimizeByConflictSwaps =
        [&](CardCalloutColumns& columns,
            const CardCalloutColumns& preferred,
            const LayoutGuideSheetCardPlacement& placement) {
            size_t passes = 0;
            for (; passes < kMaxAdjacentOrderPasses; ++passes) {
                const LeaderScore current = collectLeaderScore(columns, placement);
                if (current.score == 0 || current.conflicts.empty()) {
                    return passes;
                }

                CardCalloutColumns bestColumns = columns;
                int bestScore = current.score;
                int bestPenalty = placementPenalty(columns, preferred);

                const auto consider = [&](const CardCalloutColumns& candidate) {
                    const int score = countLeaderIntersections(candidate, placement, bestScore);
                    const int penalty = placementPenalty(candidate, preferred);
                    if (score < bestScore || (score == bestScore && penalty < bestPenalty)) {
                        bestScore = score;
                        bestPenalty = penalty;
                        bestColumns = candidate;
                    }
                };

                const auto considerNeighborSwaps = [&](size_t plannedIndex) {
                    const auto location = findPlannedIndex(columns, plannedIndex);
                    if (!location.found) {
                        return;
                    }
                    const std::vector<size_t>& stack = stackForSide(columns, location.side);
                    if (location.index > 0) {
                        CardCalloutColumns candidate = columns;
                        swapPlannedIndexes(candidate, plannedIndex, stack[location.index - 1]);
                        consider(candidate);
                    }
                    if (location.index + 1 < stack.size()) {
                        CardCalloutColumns candidate = columns;
                        swapPlannedIndexes(candidate, plannedIndex, stack[location.index + 1]);
                        consider(candidate);
                    }
                };

                const auto considerSwapsWithPlacedCallouts = [&](size_t plannedIndex) {
                    const LayoutGuideSheetExitSide sides[]{LayoutGuideSheetExitSide::Top,
                        LayoutGuideSheetExitSide::Left,
                        LayoutGuideSheetExitSide::Right,
                        LayoutGuideSheetExitSide::Bottom};
                    for (const LayoutGuideSheetExitSide side : sides) {
                        const std::vector<size_t>& stack = stackForSide(columns, side);
                        for (const size_t other : stack) {
                            CardCalloutColumns candidate = columns;
                            if (swapPlannedIndexes(candidate, plannedIndex, other)) {
                                consider(candidate);
                            }
                        }
                    }
                };

                const auto considerSideMoves = [&](size_t plannedIndex) {
                    const LayoutGuideSheetExitSide sides[]{
                        LayoutGuideSheetExitSide::Left, LayoutGuideSheetExitSide::Right};
                    const auto originalLocation = findPlannedIndex(columns, plannedIndex);
                    if (!originalLocation.found) {
                        return;
                    }
                    const bool fromSide = originalLocation.side == LayoutGuideSheetExitSide::Left ||
                        originalLocation.side == LayoutGuideSheetExitSide::Right;
                    for (const LayoutGuideSheetExitSide side : sides) {
                        const std::vector<size_t>& stack = stackForSide(columns, side);
                        for (size_t insertAt = 0; insertAt <= stack.size(); ++insertAt) {
                            if (!fromSide) {
                                continue;
                            }
                            if (side == originalLocation.side &&
                                (insertAt == originalLocation.index || insertAt == originalLocation.index + 1)) {
                                continue;
                            }
                            CardCalloutColumns candidate = columns;
                            if (!removePlannedIndex(candidate, plannedIndex)) {
                                continue;
                            }
                            size_t adjustedInsertAt = insertAt;
                            if (side == originalLocation.side && adjustedInsertAt > originalLocation.index) {
                                --adjustedInsertAt;
                            }
                            std::vector<size_t>& candidateStack = stackForSide(candidate, side);
                            adjustedInsertAt = std::min(adjustedInsertAt, candidateStack.size());
                            candidateStack.insert(
                                candidateStack.begin() + static_cast<std::ptrdiff_t>(adjustedInsertAt), plannedIndex);
                            consider(candidate);
                        }
                    }
                    if (fromSide) {
                        return;
                    }
                    for (const LayoutGuideSheetExitSide side : sides) {
                        const std::vector<size_t>& stack = stackForSide(columns, side);
                        for (size_t replacementIndex = 0; replacementIndex < stack.size(); ++replacementIndex) {
                            for (size_t insertAt = 0; insertAt <= stack.size(); ++insertAt) {
                                CardCalloutColumns candidate = columns;
                                const size_t replacement = stack[replacementIndex];
                                if (!removePlannedIndex(candidate, plannedIndex) ||
                                    !removePlannedIndex(candidate, replacement)) {
                                    continue;
                                }
                                std::vector<size_t>& promotedStack = stackForSide(candidate, originalLocation.side);
                                promotedStack.push_back(replacement);
                                size_t adjustedInsertAt = insertAt;
                                if (adjustedInsertAt > replacementIndex) {
                                    --adjustedInsertAt;
                                }
                                std::vector<size_t>& candidateStack = stackForSide(candidate, side);
                                adjustedInsertAt = std::min(adjustedInsertAt, candidateStack.size());
                                candidateStack.insert(
                                    candidateStack.begin() + static_cast<std::ptrdiff_t>(adjustedInsertAt),
                                    plannedIndex);
                                consider(candidate);
                            }
                        }
                    }
                };

                for (const LeaderConflict& conflict : current.conflicts) {
                    CardCalloutColumns candidate = columns;
                    if (swapPlannedIndexes(candidate, conflict.first, conflict.second)) {
                        consider(candidate);
                    }
                    considerNeighborSwaps(conflict.first);
                    considerNeighborSwaps(conflict.second);
                    considerSwapsWithPlacedCallouts(conflict.first);
                    considerSwapsWithPlacedCallouts(conflict.second);
                    considerSideMoves(conflict.first);
                    considerSideMoves(conflict.second);
                }

                if (bestScore > current.score ||
                    (bestScore == current.score && bestPenalty >= placementPenalty(columns, preferred))) {
                    return passes;
                }
                columns = std::move(bestColumns);
            }
            return passes;
        };

    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        const CardCalloutColumns preferredSplit = plannedByCard[cardIndex];
        CardCalloutColumns columns = preferredSplit;
        promoteInitialTopBottom(columns);
        columns.leaderRepairPasses =
            static_cast<int>(optimizeByConflictSwaps(columns, preferredSplit, cardPlacements[cardIndex]));
        plannedByCard[cardIndex] = std::move(columns);
    }

    const auto sideStackRect =
        [&](const std::vector<size_t>& plannedIndexes, LayoutGuideSheetExitSide side, const BlockLayout& block) {
            if (plannedIndexes.empty()) {
                return RenderRect{};
            }
            const int height = stackedHeight(plannedIndexes);
            const int top = block.itemY + block.itemHeight / 2 - height / 2;
            if (side == LayoutGuideSheetExitSide::Left) {
                return RenderRect{block.itemX - style.calloutGap - block.leftWidth,
                    top,
                    block.itemX - style.calloutGap,
                    top + height};
            }
            return RenderRect{block.itemX + block.itemWidth + style.calloutGap,
                top,
                block.itemX + block.itemWidth + style.calloutGap + block.rightWidth,
                top + height};
        };

    const auto topBottomBubbleRect = [&](size_t plannedIndex, LayoutGuideSheetExitSide side, const BlockLayout& block) {
        const LayoutGuideSheetPlacementCallout& callout = callouts[plannedCallouts[plannedIndex].calloutIndex];
        const int x = block.itemX + (block.itemWidth - callout.bubbleRect.Width()) / 2;
        const int y = side == LayoutGuideSheetExitSide::Top ?
            block.itemY - style.calloutGap - callout.bubbleRect.Height() :
            block.itemY + block.itemHeight + style.calloutGap;
        return RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
    };

    const auto constrainTopBottomIfNeeded =
        [&](const std::vector<size_t>& plannedIndexes,
            LayoutGuideSheetExitSide side,
            const CardCalloutColumns& columns,
            const BlockLayout& block) {
            bool changed = false;
            const RenderRect leftStack = sideStackRect(columns.left, LayoutGuideSheetExitSide::Left, block);
            const RenderRect rightStack = sideStackRect(columns.right, LayoutGuideSheetExitSide::Right, block);
            for (const size_t plannedIndex : plannedIndexes) {
                LayoutGuideSheetPlacementCallout& callout = callouts[plannedCallouts[plannedIndex].calloutIndex];
                if (callout.bubbleRect.Width() <= block.itemWidth) {
                    continue;
                }
                const RenderRect bubble = topBottomBubbleRect(plannedIndex, side, block);
                if (!RectsOverlap(bubble, leftStack) && !RectsOverlap(bubble, rightStack)) {
                    continue;
                }
                constrainCalloutWidth(callout, std::max(1, block.itemWidth));
                changed = true;
            }
            return changed;
        };

    for (size_t pass = 0; pass < 3; ++pass) {
        bool changed = false;
        for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
            const BlockLayout block = computeBlockForColumns(plannedByCard[cardIndex], cardPlacements[cardIndex]);
            changed =
                constrainTopBottomIfNeeded(
                    plannedByCard[cardIndex].top, LayoutGuideSheetExitSide::Top, plannedByCard[cardIndex], block) ||
                changed;
            changed =
                constrainTopBottomIfNeeded(plannedByCard[cardIndex].bottom,
                    LayoutGuideSheetExitSide::Bottom,
                    plannedByCard[cardIndex],
                    block) ||
                changed;
        }
        if (!changed) {
            break;
        }
    }
    std::vector<BlockLayout> blocks(cardPlacements.size());
    int contentWidth = 0;
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        blocks[cardIndex] = computeBlockForColumns(plannedByCard[cardIndex], cardPlacements[cardIndex]);
        contentWidth = std::max(contentWidth, blocks[cardIndex].width);
    }

    const auto placeSide =
        [&](const std::vector<size_t>& plannedIndexes,
            LayoutGuideSheetExitSide side,
            const RenderRect& cardRect,
            const BlockLayout& block) {
            int y = cardRect.Center().y - stackedHeight(plannedIndexes) / 2;
            for (const size_t plannedIndex : plannedIndexes) {
                const PlannedCallout& planned = plannedCallouts[plannedIndex];
                LayoutGuideSheetPlacementCallout& callout = callouts[planned.calloutIndex];
                const int x = side == LayoutGuideSheetExitSide::Left ?
                    block.itemX - style.calloutGap - callout.bubbleRect.Width() :
                    block.itemX + block.itemWidth + style.calloutGap;
                callout.bubbleRect = RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
                callout.exitSide = side;
                callout.bubbleAttachment = RenderPoint{
                    side == LayoutGuideSheetExitSide::Left ? callout.bubbleRect.right : callout.bubbleRect.left,
                    callout.bubbleRect.Center().y};
                const int dx = cardRect.left - cardPlacements[planned.cardIndex].sourceRect.left;
                const int dy = cardRect.top - cardPlacements[planned.cardIndex].sourceRect.top;
                const RenderRect targetRect = cardPlacements[planned.cardIndex].overview ?
                    TransformRect(planned.target,
                        cardPlacements[planned.cardIndex].sourceRect,
                        cardPlacements[planned.cardIndex].destRect) :
                    OffsetRenderRect(planned.target, dx, dy);
                callout.targetAttachment =
                    TargetAttachmentForCallout(callout, targetRect, callout.bubbleAttachment, style.gaugeRingThickness);
                y = callout.bubbleRect.bottom + style.rowGap;
            }
        };

    const auto placeTopBottom =
        [&](const std::vector<size_t>& plannedIndexes,
            LayoutGuideSheetExitSide side,
            const RenderRect& cardRect,
            const BlockLayout& block) {
            for (const size_t plannedIndex : plannedIndexes) {
                const PlannedCallout& planned = plannedCallouts[plannedIndex];
                LayoutGuideSheetPlacementCallout& callout = callouts[planned.calloutIndex];
                const int x = block.itemX + (block.itemWidth - callout.bubbleRect.Width()) / 2;
                const int y = side == LayoutGuideSheetExitSide::Top ?
                    block.itemY - style.calloutGap - callout.bubbleRect.Height() :
                    block.itemY + block.itemHeight + style.calloutGap;
                callout.bubbleRect = RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
                callout.exitSide = side;
                callout.bubbleAttachment = RenderPoint{callout.bubbleRect.Center().x,
                    side == LayoutGuideSheetExitSide::Top ? callout.bubbleRect.bottom : callout.bubbleRect.top};
                const int dx = cardRect.left - cardPlacements[planned.cardIndex].sourceRect.left;
                const int dy = cardRect.top - cardPlacements[planned.cardIndex].sourceRect.top;
                const RenderRect targetRect = cardPlacements[planned.cardIndex].overview ?
                    TransformRect(planned.target,
                        cardPlacements[planned.cardIndex].sourceRect,
                        cardPlacements[planned.cardIndex].destRect) :
                    OffsetRenderRect(planned.target, dx, dy);
                callout.targetAttachment =
                    TargetAttachmentForCallout(callout, targetRect, callout.bubbleAttachment, style.gaugeRingThickness);
            }
        };

    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        LayoutGuideSheetCardPlacement& placement = cardPlacements[cardIndex];
        const BlockLayout& block = blocks[cardIndex];
        placement.destRect =
            RenderRect{block.itemX, block.itemY, block.itemX + block.itemWidth, block.itemY + block.itemHeight};
        placeTopBottom(plannedByCard[cardIndex].top, LayoutGuideSheetExitSide::Top, placement.destRect, block);
        placeSide(plannedByCard[cardIndex].left, LayoutGuideSheetExitSide::Left, placement.destRect, block);
        placeSide(plannedByCard[cardIndex].right, LayoutGuideSheetExitSide::Right, placement.destRect, block);
        placeTopBottom(plannedByCard[cardIndex].bottom, LayoutGuideSheetExitSide::Bottom, placement.destRect, block);
    }

    result.sheetWidth = style.sheetMargin * 2 + contentWidth;
    int blockCursorY = style.sheetMargin;
    int contentBottom = style.sheetMargin;
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        const int dx = style.sheetMargin + (contentWidth - blocks[cardIndex].width) / 2;
        const int dy = blockCursorY;
        LayoutGuideSheetCardPlacement& placement = cardPlacements[cardIndex];
        placement.destRect = OffsetRenderRect(placement.destRect, dx, dy);
        contentBottom = std::max(contentBottom, placement.destRect.bottom);
        const auto offsetCallouts = [&](const std::vector<size_t>& plannedIndexes) {
            for (size_t plannedIndex : plannedIndexes) {
                LayoutGuideSheetPlacementCallout& callout = callouts[plannedCallouts[plannedIndex].calloutIndex];
                callout.bubbleRect = OffsetRenderRect(callout.bubbleRect, dx, dy);
                contentBottom = std::max(contentBottom, callout.bubbleRect.bottom);
                callout.targetAttachment.x += dx;
                callout.targetAttachment.y += dy;
                callout.bubbleAttachment.x += dx;
                callout.bubbleAttachment.y += dy;
            }
        };
        offsetCallouts(plannedByCard[cardIndex].top);
        offsetCallouts(plannedByCard[cardIndex].left);
        offsetCallouts(plannedByCard[cardIndex].right);
        offsetCallouts(plannedByCard[cardIndex].bottom);
        blockCursorY += blocks[cardIndex].advanceHeight + style.blockGap;
    }
    result.sheetHeight = cardPlacements.empty() ? style.sheetMargin * 2 : contentBottom + style.sheetMargin;

    if (traceDetails != nullptr) {
        std::vector<size_t> leaders;
        leaders.reserve(callouts.size());
        for (size_t calloutIndex = 0; calloutIndex < callouts.size(); ++calloutIndex) {
            const LayoutGuideSheetPlacementCallout& callout = callouts[calloutIndex];
            const size_t sourceCardIndex = cardOrder(callout.sourceCardId);
            const auto calloutKey = [&](size_t index) -> std::string_view {
                return index < callouts.size() ? callouts[index].key : std::string_view{};
            };
            const auto sourceCardId = [&]() -> std::string_view {
                if (sourceCardIndex < cardPlacements.size()) {
                    return cardPlacements[sourceCardIndex].id;
                }
                return callout.sourceCardId;
            };
            const auto recordIntersection =
                [&](const char* kind,
                    size_t firstIndex,
                    size_t secondIndex,
                    LayoutGuideSheetExitSide firstSide,
                    LayoutGuideSheetExitSide secondSide) {
                    const std::string_view cardId = sourceCardId();
                    const std::string_view firstKey = calloutKey(firstIndex);
                    const std::string_view secondKey = calloutKey(secondIndex);
                    traceDetails->push_back(FormatText(
                        ResourceStringText(RES_STR(
                            "intersection_card=\"%.*s\" intersection_kind=\"%s\" first_side=\"%s\" "
                            "first_callout=\"%.*s\" second_side=\"%s\" second_callout=\"%.*s\"")),
                        static_cast<int>(cardId.size()),
                        cardId.data(),
                        kind,
                        ExitSideName(firstSide),
                        static_cast<int>(firstKey.size()),
                        firstKey.data(),
                        ExitSideName(secondSide),
                        static_cast<int>(secondKey.size()),
                        secondKey.data()));
                };
            for (size_t leaderIndex : leaders) {
                const LayoutGuideSheetPlacementCallout& leader = callouts[leaderIndex];
                if (callout.sourceCardId != leader.sourceCardId) {
                    continue;
                }
                if (LeaderSegmentsIntersect(callout.targetAttachment,
                        callout.bubbleAttachment,
                        leader.targetAttachment,
                        leader.bubbleAttachment)) {
                    recordIntersection("leader_cross", calloutIndex, leaderIndex, callout.exitSide, leader.exitSide);
                }
                if (SegmentIntersectsRect(callout.targetAttachment,
                        callout.bubbleAttachment,
                        TargetSafeRect(leader.targetAttachment, style.targetSafeRadius))) {
                    recordIntersection(
                        "target_safe_zone", calloutIndex, leaderIndex, callout.exitSide, leader.exitSide);
                }
                if (SegmentIntersectsRect(leader.targetAttachment,
                        leader.bubbleAttachment,
                        TargetSafeRect(callout.targetAttachment, style.targetSafeRadius))) {
                    recordIntersection(
                        "target_safe_zone", leaderIndex, calloutIndex, leader.exitSide, callout.exitSide);
                }
            }
            leaders.push_back(calloutIndex);
        }

        for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
            const CardCalloutColumns& columns = plannedByCard[cardIndex];
            const int leaderScore = countLeaderIntersections(columns, cardPlacements[cardIndex]);
            const std::string& cardId = cardPlacements[cardIndex].id;
            traceDetails->push_back(FormatText(
                ResourceStringText(
                    RES_STR("leader_score_%s=%d leader_repair_passes_%s=%d leader_columns_%s=\"%zu,%zu,%zu,%zu\"")),
                cardId.c_str(),
                leaderScore,
                cardId.c_str(),
                columns.leaderRepairPasses,
                cardId.c_str(),
                columns.left.size(),
                columns.top.size(),
                columns.right.size(),
                columns.bottom.size()));
        }
    }
    return result;
}
