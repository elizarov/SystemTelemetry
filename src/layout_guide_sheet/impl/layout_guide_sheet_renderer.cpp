#include "layout_guide_sheet/impl/layout_guide_sheet_renderer.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_placement.h"
#include "layout_model/layout_edit_anchor_shape.h"
#include "layout_model/layout_edit_helpers.h"
#include "util/text_format.h"

namespace {

RenderRect MakeOverviewSquareAnchorRect(int centerX, int centerY, int size) {
    const int half = size / 2;
    return RenderRect{centerX - half, centerY - half, centerX - half + size, centerY - half + size};
}

RenderRect CenteredSquare(RenderPoint center, int size) {
    const int half = size / 2;
    return RenderRect{center.x - half, center.y - half, center.x - half + size, center.y - half + size};
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

int ScaleNonNegative(DashboardRenderer& renderer, int value) {
    return std::max(0, renderer.ScaleLogical(value));
}

int ScaleAtLeast(DashboardRenderer& renderer, int value, int minimum) {
    return std::max(minimum, renderer.ScaleLogical(value));
}

bool CalloutPriorityLess(const LayoutGuideSheetPlacementCallout& lhs, const LayoutGuideSheetPlacementCallout& rhs) {
    if (lhs.priority != rhs.priority) {
        return lhs.priority < rhs.priority;
    }
    return lhs.order < rhs.order;
}

void StableSortCalloutsByPriority(std::vector<LayoutGuideSheetPlacementCallout>& callouts) {
    // Size: callout lists are small; insertion sort avoids std::stable_sort template code.
    for (size_t i = 1; i < callouts.size(); ++i) {
        LayoutGuideSheetPlacementCallout current = std::move(callouts[i]);
        size_t j = i;
        while (j > 0 && CalloutPriorityLess(current, callouts[j - 1])) {
            callouts[j] = std::move(callouts[j - 1]);
            --j;
        }
        callouts[j] = std::move(current);
    }
}

struct PackedOverviewCard {
    std::string id;
    RenderRect rect{};
    LayoutGuideSheetCardChromeArtifacts chromeArtifacts;
};

struct PackedOverview {
    RenderRect rect{};
    std::vector<PackedOverviewCard> cards;
    std::vector<LayoutEditGuide> guides;
    std::vector<LayoutEditGapAnchor> gapAnchors;
    std::vector<LayoutEditAnchorRegion> reorderAnchors;
};

struct PackedNode {
    int width = 0;
    int height = 0;
};

const LayoutCardConfig* FindCardConfig(const AppConfig& config, const std::string& id) {
    const auto it = std::find_if(
        config.layout.cards.begin(), config.layout.cards.end(), [&](const auto& card) { return card.id == id; });
    return it != config.layout.cards.end() ? &(*it) : nullptr;
}

PackedNode MeasurePackedNode(const LayoutNodeConfig& node, DashboardRenderer& renderer) {
    if (node.children.empty() || node.cardReference) {
        const LayoutCardConfig* card = FindCardConfig(renderer.Config(), node.name);
        if (card == nullptr) {
            return {};
        }
        const CardChromeLayoutMetrics metrics = ResolveCardChromeLayoutMetrics(renderer);
        const int titleWidth =
            card->title.empty() ? 0 : renderer.Renderer().MeasureTextWidth(TextStyleId::Title, card->title);
        const int iconWidth = card->icon.empty() ? 0 : metrics.iconSize;
        const int headerGap = (!card->title.empty() && !card->icon.empty()) ? metrics.iconGap : 0;
        const int headerWidth = iconWidth + headerGap + titleWidth;
        const int headerHeight =
            std::max(card->icon.empty() ? 0 : metrics.iconSize, card->title.empty() ? 0 : metrics.titleHeight);
        const int width = std::max(1, metrics.padding * 2 + headerWidth);
        const int height = std::max(1, metrics.padding * 2 + headerHeight);
        return PackedNode{width, height};
    }

    const bool horizontal = node.name == "columns";
    const int gap = renderer.ScaleLogical(
        horizontal ? renderer.Config().layout.dashboard.columnGap : renderer.Config().layout.dashboard.rowGap);
    std::vector<PackedNode> children;
    children.reserve(node.children.size());
    for (const LayoutNodeConfig& child : node.children) {
        children.push_back(MeasurePackedNode(child, renderer));
    }

    PackedNode result;
    if (horizontal) {
        for (size_t i = 0; i < children.size(); ++i) {
            result.width += children[i].width;
            if (i + 1 < children.size()) {
                result.width += gap;
            }
            result.height = std::max(result.height, children[i].height);
        }
    } else {
        for (size_t i = 0; i < children.size(); ++i) {
            result.height += children[i].height;
            if (i + 1 < children.size()) {
                result.height += gap;
            }
            result.width = std::max(result.width, children[i].width);
        }
    }
    result.width = std::max(1, result.width);
    result.height = std::max(1, result.height);
    return result;
}

bool SameLayoutGuideIdentity(const LayoutEditGuide& lhs, const LayoutEditGuide& rhs) {
    return lhs.renderCardId == rhs.renderCardId && lhs.editCardId == rhs.editCardId && lhs.nodePath == rhs.nodePath &&
           lhs.separatorIndex == rhs.separatorIndex;
}

bool SameGapAnchorIdentity(const LayoutEditGapAnchor& lhs, const LayoutEditGapAnchorKey& rhs) {
    return lhs.key.widget.kind == rhs.widget.kind && lhs.key.widget.renderCardId == rhs.widget.renderCardId &&
           lhs.key.widget.editCardId == rhs.widget.editCardId && lhs.key.parameter == rhs.parameter &&
           lhs.key.nodePath == rhs.nodePath;
}

bool SameEditableAnchorIdentity(const LayoutEditAnchorRegion& lhs, const LayoutEditAnchorKey& rhs) {
    return MatchesEditableAnchorKey(lhs.key, rhs);
}

void AddPackedDashboardGuides(PackedOverview& overview,
    DashboardRenderer& renderer,
    const LayoutNodeConfig& node,
    const RenderRect& rect,
    const std::vector<RenderRect>& childRects,
    int gap,
    const std::vector<size_t>& nodePath) {
    if (node.children.size() < 2) {
        return;
    }
    const bool horizontal = node.name == "columns";
    const LayoutGuideSheetConfig& sheetStyle = renderer.Config().layout.layoutGuideSheet;
    const int hitInset = ScaleAtLeast(renderer, sheetStyle.overviewGuideHitInset, 1);
    const int reorderWidth = ScaleAtLeast(renderer, horizontal ? 12 : 8, 1);
    const int reorderHeight = ScaleAtLeast(renderer, horizontal ? 8 : 12, 1);
    const int reorderInset = ScaleAtLeast(renderer, 3, 1);
    for (size_t i = 0; i < childRects.size(); ++i) {
        const RenderRect& childRect = childRects[i];
        if (childRect.IsEmpty()) {
            continue;
        }
        const int centerX =
            horizontal ? childRect.left + childRect.Width() / 2 : childRect.right - (reorderWidth / 2) - reorderInset;
        const int centerY =
            horizontal ? childRect.top + (reorderHeight / 2) + reorderInset : childRect.top + childRect.Height() / 2;
        const RenderRect anchorRect{centerX - reorderWidth / 2,
            centerY - reorderHeight / 2,
            centerX - reorderWidth / 2 + reorderWidth,
            centerY - reorderHeight / 2 + reorderHeight};
        LayoutEditAnchorRegion anchor;
        anchor.key = LayoutEditAnchorKey{
            LayoutEditWidgetIdentity{"", "", nodePath, LayoutEditWidgetIdentity::Kind::DashboardChrome},
            LayoutContainerChildOrderEditKey{"", nodePath},
            static_cast<int>(i)};
        anchor.targetRect = childRect;
        anchor.anchorRect = anchorRect;
        anchor.anchorHitRect = anchorRect.Inflate(hitInset, hitInset);
        anchor.anchorHitPadding = hitInset;
        anchor.shape = horizontal ? AnchorShape::HorizontalReorder : AnchorShape::VerticalReorder;
        anchor.dragAxis = horizontal ? AnchorDragAxis::Horizontal : AnchorDragAxis::Vertical;
        anchor.showWhenWidgetHovered = true;
        anchor.drawTargetOutline = false;
        overview.reorderAnchors.push_back(std::move(anchor));
    }

    const LayoutEditParameter gapParameter =
        horizontal ? LayoutEditParameter::DashboardColumnGap : LayoutEditParameter::DashboardRowGap;
    const bool gapAnchorAlreadyRegistered = std::any_of(overview.gapAnchors.begin(),
        overview.gapAnchors.end(),
        [&](const auto& anchor) { return anchor.key.parameter == gapParameter; });
    if (!gapAnchorAlreadyRegistered) {
        LayoutEditGapAnchor anchor;
        anchor.axis = horizontal ? LayoutGuideAxis::Horizontal : LayoutGuideAxis::Vertical;
        anchor.key.widget = LayoutEditWidgetIdentity{"", "", {}, LayoutEditWidgetIdentity::Kind::DashboardChrome};
        anchor.key.parameter = gapParameter;
        anchor.key.nodePath = nodePath;
        const RenderRect& lead = childRects.front();
        const RenderRect& trail = childRects[1];
        const int handleSize = ScaleAtLeast(renderer, sheetStyle.overviewGapHandleSize, 1);
        if (horizontal) {
            anchor.drawStart = RenderPoint{lead.right, rect.top};
            anchor.drawEnd = RenderPoint{trail.left, rect.top};
            anchor.dragAxis = AnchorDragAxis::Horizontal;
            anchor.handleRect = MakeOverviewSquareAnchorRect(anchor.drawEnd.x, anchor.drawEnd.y, handleSize);
            anchor.value = renderer.Config().layout.dashboard.columnGap;
        } else {
            anchor.drawStart = RenderPoint{rect.left, lead.bottom};
            anchor.drawEnd = RenderPoint{rect.left, trail.top};
            anchor.dragAxis = AnchorDragAxis::Vertical;
            anchor.handleRect = MakeOverviewSquareAnchorRect(anchor.drawEnd.x, anchor.drawEnd.y, handleSize);
            anchor.value = renderer.Config().layout.dashboard.rowGap;
        }
        anchor.hitRect = anchor.handleRect.Inflate(hitInset, hitInset);
        overview.gapAnchors.push_back(std::move(anchor));
    }

    for (size_t i = 0; i + 1 < childRects.size(); ++i) {
        LayoutEditGuide guide;
        guide.axis = horizontal ? LayoutGuideAxis::Vertical : LayoutGuideAxis::Horizontal;
        guide.nodePath = nodePath;
        guide.separatorIndex = i;
        guide.containerRect = rect;
        guide.gap = gap;
        guide.childRects = childRects;
        guide.childFixedExtents.assign(childRects.size(), 0u);
        guide.childExtents.reserve(childRects.size());
        for (const RenderRect& childRect : childRects) {
            guide.childExtents.push_back(horizontal ? childRect.Width() : childRect.Height());
        }
        if (horizontal) {
            const int x = childRects[i].right + std::max(0, gap / 2);
            guide.lineRect = RenderRect{x, rect.top, x + 1, rect.bottom};
            guide.hitRect = RenderRect{x - hitInset, rect.top, x + hitInset + 1, rect.bottom};
        } else {
            const int y = childRects[i].bottom + std::max(0, gap / 2);
            guide.lineRect = RenderRect{rect.left, y, rect.right, y + 1};
            guide.hitRect = RenderRect{rect.left, y - hitInset, rect.right, y + hitInset + 1};
        }
        overview.guides.push_back(std::move(guide));
    }
}

void AppendPackedCards(const LayoutNodeConfig& node,
    DashboardRenderer& renderer,
    const RenderRect& rect,
    const std::vector<size_t>& nodePath,
    PackedOverview& overview) {
    if (node.children.empty() || node.cardReference) {
        const LayoutCardConfig* card = FindCardConfig(renderer.Config(), node.name);
        if (card == nullptr) {
            return;
        }
        PackedOverviewCard packedCard;
        packedCard.id = card->id;
        packedCard.rect = rect;
        overview.cards.push_back(std::move(packedCard));
        return;
    }

    const bool horizontal = node.name == "columns";
    const int gap = renderer.ScaleLogical(
        horizontal ? renderer.Config().layout.dashboard.columnGap : renderer.Config().layout.dashboard.rowGap);
    std::vector<PackedNode> measured;
    measured.reserve(node.children.size());
    for (const LayoutNodeConfig& child : node.children) {
        measured.push_back(MeasurePackedNode(child, renderer));
    }
    const int totalGap = gap * static_cast<int>(node.children.empty() ? 0 : node.children.size() - 1);
    const int available = std::max(0, (horizontal ? rect.Width() : rect.Height()) - totalGap);
    int totalWeight = 0;
    int totalMin = 0;
    for (size_t i = 0; i < node.children.size(); ++i) {
        totalWeight += std::max(1, node.children[i].weight);
        totalMin += horizontal ? measured[i].width : measured[i].height;
    }
    int remainingExtra = std::max(0, available - totalMin);
    int remainingWeight = std::max(1, totalWeight);
    int cursor = horizontal ? rect.left : rect.top;
    std::vector<RenderRect> childRects;
    childRects.reserve(node.children.size());
    std::vector<size_t> childPath = nodePath;
    for (size_t i = 0; i < node.children.size(); ++i) {
        const int weight = std::max(1, node.children[i].weight);
        const int minExtent = horizontal ? measured[i].width : measured[i].height;
        const int extra =
            i + 1 == node.children.size() ? remainingExtra : (remainingExtra * weight / std::max(1, remainingWeight));
        const int extent = minExtent + extra;
        RenderRect childRect = rect;
        if (horizontal) {
            childRect.left = cursor;
            childRect.right = cursor + extent;
        } else {
            childRect.top = cursor;
            childRect.bottom = cursor + extent;
        }
        childRects.push_back(childRect);
        childPath.push_back(i);
        AppendPackedCards(node.children[i], renderer, childRect, childPath, overview);
        childPath.pop_back();
        cursor += extent + gap;
        remainingExtra -= extra;
        remainingWeight -= weight;
    }
    AddPackedDashboardGuides(overview, renderer, node, rect, childRects, gap, nodePath);
}

PackedOverview BuildPackedOverview(DashboardRenderer& renderer) {
    PackedNode root = MeasurePackedNode(renderer.Config().layout.structure.cards, renderer);
    const int outerMargin = renderer.ScaleLogical(renderer.Config().layout.dashboard.outerMargin);
    PackedOverview overview;
    overview.rect = RenderRect{0, 0, root.width + outerMargin * 2, root.height + outerMargin * 2};
    const LayoutGuideSheetConfig& sheetStyle = renderer.Config().layout.layoutGuideSheet;
    LayoutEditGapAnchor outerMarginAnchor;
    outerMarginAnchor.axis = LayoutGuideAxis::Horizontal;
    outerMarginAnchor.key.widget =
        LayoutEditWidgetIdentity{"", "", {}, LayoutEditWidgetIdentity::Kind::DashboardChrome};
    outerMarginAnchor.key.parameter = LayoutEditParameter::DashboardOuterMargin;
    outerMarginAnchor.drawStart = RenderPoint{0, outerMargin};
    outerMarginAnchor.drawEnd = RenderPoint{outerMargin, outerMargin};
    outerMarginAnchor.dragAxis = AnchorDragAxis::Horizontal;
    outerMarginAnchor.handleRect = MakeOverviewSquareAnchorRect(
        outerMargin, outerMargin, ScaleAtLeast(renderer, sheetStyle.overviewGapHandleSize, 1));
    outerMarginAnchor.hitRect =
        outerMarginAnchor.handleRect.Inflate(ScaleAtLeast(renderer, sheetStyle.overviewGuideHitInset, 1),
            ScaleAtLeast(renderer, sheetStyle.overviewGuideHitInset, 1));
    outerMarginAnchor.value = renderer.Config().layout.dashboard.outerMargin;
    overview.gapAnchors.push_back(std::move(outerMarginAnchor));
    AppendPackedCards(renderer.Config().layout.structure.cards,
        renderer,
        RenderRect{outerMargin, outerMargin, outerMargin + root.width, outerMargin + root.height},
        {},
        overview);
    return overview;
}

LayoutEditActiveRegions CollectActiveRegionsFromPackedOverview(const PackedOverview& overview) {
    LayoutEditActiveRegions regions;
    size_t activeRegionCount = overview.cards.size() * 2 + overview.guides.size() + overview.gapAnchors.size() +
                               overview.reorderAnchors.size() * 2;
    for (const PackedOverviewCard& card : overview.cards) {
        activeRegionCount += card.chromeArtifacts.widgetGuides.size() + card.chromeArtifacts.anchorRegions.size() * 2 +
                             card.chromeArtifacts.colorRegions.size();
    }
    regions.Reserve(activeRegionCount);
    const auto appendRegion =
        [&](const RenderRect& box, LayoutEditActiveRegionKind kind, LayoutEditActiveRegionPayload payload) {
            if (box.IsEmpty()) {
                return;
            }
            regions.Add(LayoutEditActiveRegion{box, kind, std::move(payload)});
        };

    for (const PackedOverviewCard& card : overview.cards) {
        LayoutEditCardRegion cardRegion{card.id,
            {},
            card.rect,
            card.chromeArtifacts.chromeLayout.titleRect,
            card.chromeArtifacts.chromeLayout.hasHeader};
        appendRegion(card.rect, LayoutEditActiveRegionKind::Card, cardRegion);
        if (card.chromeArtifacts.chromeLayout.hasHeader) {
            appendRegion(
                card.chromeArtifacts.chromeLayout.titleRect, LayoutEditActiveRegionKind::CardHeader, cardRegion);
        }
        for (const LayoutEditWidgetGuide& guide : card.chromeArtifacts.widgetGuides) {
            appendRegion(guide.hitRect, LayoutEditActiveRegionKind::WidgetGuide, guide);
        }
        for (const LayoutEditAnchorRegion& anchor : card.chromeArtifacts.anchorRegions) {
            appendRegion(anchor.anchorHitRect, LayoutEditActiveRegionKind::StaticEditAnchorHandle, anchor);
            appendRegion(anchor.targetRect, LayoutEditActiveRegionKind::StaticEditAnchorTarget, anchor);
        }
        for (const LayoutEditColorRegion& color : card.chromeArtifacts.colorRegions) {
            appendRegion(color.targetRect, LayoutEditActiveRegionKind::DynamicColorTarget, color);
        }
    }
    for (const LayoutEditGuide& guide : overview.guides) {
        appendRegion(guide.hitRect, LayoutEditActiveRegionKind::LayoutWeightGuide, guide);
    }
    for (const LayoutEditGapAnchor& anchor : overview.gapAnchors) {
        appendRegion(anchor.hitRect, LayoutEditActiveRegionKind::GapHandle, anchor);
    }
    for (const LayoutEditAnchorRegion& anchor : overview.reorderAnchors) {
        appendRegion(anchor.anchorHitRect, LayoutEditActiveRegionKind::StaticEditAnchorHandle, anchor);
        appendRegion(anchor.targetRect, LayoutEditActiveRegionKind::StaticEditAnchorTarget, anchor);
    }
    return regions;
}

void DrawDottedOverviewRect(DashboardRenderer& renderer, const RenderRect& rect) {
    if (rect.IsEmpty()) {
        return;
    }
    const LayoutGuideSheetConfig& sheetStyle = renderer.Config().layout.layoutGuideSheet;
    const int padding = ScaleAtLeast(renderer, sheetStyle.overviewDottedPadding, 0);
    const RenderRect drawRect = rect.Inflate(padding, padding);
    const int strokeWidth = ScaleAtLeast(renderer, sheetStyle.overviewDottedStrokeWidth, 1);
    const int dotLength = std::max(strokeWidth + 1, renderer.ScaleLogical(sheetStyle.overviewDottedDashLength));
    const int gapLength = std::max(strokeWidth + 1, renderer.ScaleLogical(sheetStyle.overviewDottedGapLength));
    const auto drawHorizontal = [&](int y, int left, int right) {
        for (int x = left; x < right; x += dotLength + gapLength) {
            renderer.Renderer().FillSolidRect(
                RenderRect{x, y, std::min(x + dotLength, right), y + strokeWidth}, RenderColorId::LayoutGuide);
        }
    };
    const auto drawVertical = [&](int x, int top, int bottom) {
        for (int y = top; y < bottom; y += dotLength + gapLength) {
            renderer.Renderer().FillSolidRect(
                RenderRect{x, y, x + strokeWidth, std::min(y + dotLength, bottom)}, RenderColorId::LayoutGuide);
        }
    };
    drawHorizontal(drawRect.top, drawRect.left, drawRect.right);
    drawHorizontal(std::max(drawRect.top, drawRect.bottom - strokeWidth), drawRect.left, drawRect.right);
    drawVertical(drawRect.left, drawRect.top, drawRect.bottom);
    drawVertical(std::max(drawRect.left, drawRect.right - strokeWidth), drawRect.top, drawRect.bottom);
}

void DrawOverviewArtifact(DashboardRenderer& renderer,
    const LayoutGuideSheetPlacementCallout& callout,
    const RenderRect& sourceRect,
    const RenderRect& destRect) {
    const RenderRect target = TransformRect(
        callout.hoverArtifactTargetRect.has_value() ? *callout.hoverArtifactTargetRect : callout.targetRect,
        sourceRect,
        destRect);
    const LayoutGuideSheetConfig& sheetStyle = renderer.Config().layout.layoutGuideSheet;
    RenderRect anchor;
    const bool hasAnchor = callout.hoverAnchorRect.has_value();
    if (hasAnchor) {
        anchor = TransformRect(*callout.hoverAnchorRect, sourceRect, destRect);
    }
    const RenderPoint center = target.Center();
    const auto drawGuideLine = [&](LayoutGuideAxis axis) {
        if (axis == LayoutGuideAxis::Vertical) {
            renderer.Renderer().DrawSolidLine(RenderPoint{center.x, target.top},
                RenderPoint{center.x, target.bottom},
                RenderStroke::Solid(RenderColorId::LayoutGuide,
                    static_cast<float>(ScaleAtLeast(renderer, sheetStyle.overviewGuideStrokeWidth, 1))));
        } else {
            renderer.Renderer().DrawSolidLine(RenderPoint{target.left, center.y},
                RenderPoint{target.right, center.y},
                RenderStroke::Solid(RenderColorId::LayoutGuide,
                    static_cast<float>(ScaleAtLeast(renderer, sheetStyle.overviewGuideStrokeWidth, 1))));
        }
    };
    if (callout.hoverLayoutGuide.has_value()) {
        drawGuideLine(callout.hoverLayoutGuide->axis);
        return;
    }
    if (callout.hoverWidgetGuide.has_value()) {
        drawGuideLine(callout.hoverWidgetGuide->axis);
        return;
    }
    if (callout.hoverGapAnchor.has_value()) {
        const LayoutEditGapAnchor& gapAnchor = *callout.hoverGapAnchor;
        const RenderPoint drawStart = TransformPoint(gapAnchor.drawStart, sourceRect, destRect);
        const RenderPoint drawEnd = TransformPoint(gapAnchor.drawEnd, sourceRect, destRect);
        const RenderRect handle = TransformRect(gapAnchor.handleRect, sourceRect, destRect);
        const int capHalf = ScaleAtLeast(renderer, sheetStyle.overviewGapHandleSize, 1);
        const float strokeWidth = static_cast<float>(ScaleAtLeast(renderer, sheetStyle.overviewGuideStrokeWidth, 1));
        renderer.Renderer().DrawSolidLine(
            drawStart, drawEnd, RenderStroke::Solid(RenderColorId::LayoutGuide, strokeWidth));
        if (gapAnchor.axis == LayoutGuideAxis::Vertical) {
            renderer.Renderer().DrawSolidLine(RenderPoint{drawStart.x - capHalf, drawStart.y},
                RenderPoint{drawStart.x + capHalf, drawStart.y},
                RenderStroke::Solid(RenderColorId::LayoutGuide, strokeWidth));
            renderer.Renderer().DrawSolidLine(RenderPoint{drawEnd.x - capHalf, drawEnd.y},
                RenderPoint{drawEnd.x + capHalf, drawEnd.y},
                RenderStroke::Solid(RenderColorId::LayoutGuide, strokeWidth));
        } else {
            renderer.Renderer().DrawSolidLine(RenderPoint{drawStart.x, drawStart.y - capHalf},
                RenderPoint{drawStart.x, drawStart.y + capHalf},
                RenderStroke::Solid(RenderColorId::LayoutGuide, strokeWidth));
            renderer.Renderer().DrawSolidLine(RenderPoint{drawEnd.x, drawEnd.y - capHalf},
                RenderPoint{drawEnd.x, drawEnd.y + capHalf},
                RenderStroke::Solid(RenderColorId::LayoutGuide, strokeWidth));
        }
        renderer.Renderer().FillSolidRect(handle, RenderColorId::LayoutGuide);
        return;
    }
    if (callout.hoverAnchorKey.has_value()) {
        if (callout.hoverAnchorDrawTargetOutline) {
            DrawDottedOverviewRect(renderer, target);
        }
        const int size = std::max(1,
            std::min(std::max(target.Width(), target.Height()),
                ScaleAtLeast(renderer, sheetStyle.overviewAnchorMaxSize, 1)));
        const RenderRect centeredHandle{
            center.x - size / 2, center.y - size / 2, center.x - size / 2 + size, center.y - size / 2 + size};
        const RenderRect handle = hasAnchor ? anchor : centeredHandle;
        const AnchorShape shape = callout.hoverAnchorShape.value_or(AnchorShape::Circle);
        DrawLayoutEditAnchorShape(renderer.Renderer(),
            shape,
            handle,
            RenderColorId::LayoutGuide,
            static_cast<float>(ScaleAtLeast(renderer, sheetStyle.overviewGuideStrokeWidth, 1)),
            ScaleAtLeast(renderer, 1, 1),
            false,
            false);
        return;
    }
    renderer.Renderer().DrawSolidRect(target,
        RenderStroke::Solid(RenderColorId::LayoutGuide,
            static_cast<float>(ScaleAtLeast(renderer, sheetStyle.overviewGuideStrokeWidth, 1))));
}

}  // namespace

LayoutGuideSheetRenderer::LayoutGuideSheetRenderer(DashboardRenderer& dashboardRenderer)
    : dashboardRenderer_(dashboardRenderer) {}

LayoutEditActiveRegions LayoutGuideSheetRenderer::CollectOverviewActiveRegions(const SystemSnapshot& snapshot) {
    PackedOverview overview = BuildPackedOverview(dashboardRenderer_);
    const MetricSource& metrics = dashboardRenderer_.ResolveMetrics(snapshot);
    for (PackedOverviewCard& card : overview.cards) {
        card.chromeArtifacts =
            dashboardRenderer_.BuildLayoutGuideSheetCardChromeArtifacts(card.id, card.rect, &metrics);
    }
    return CollectActiveRegionsFromPackedOverview(overview);
}

bool LayoutGuideSheetRenderer::SavePng(const FilePath& imagePath,
    const SystemSnapshot& snapshot,
    std::vector<LayoutGuideSheetCalloutRequest>& callouts,
    const std::vector<std::string>& selectedCardIds,
    std::vector<std::string>* traceDetails,
    std::string* errorText,
    LayoutGuideSheetRenderStats* stats) {
    return Render(
        snapshot,
        callouts,
        selectedCardIds,
        [&](int width, int height, SurfaceDrawCallback draw) {
            return dashboardRenderer_.SaveLayoutGuideSheetSurfacePng(imagePath, width, height, draw);
        },
        traceDetails,
        errorText,
        stats);
}

bool LayoutGuideSheetRenderer::RenderOffscreen(const SystemSnapshot& snapshot,
    std::vector<LayoutGuideSheetCalloutRequest>& callouts,
    const std::vector<std::string>& selectedCardIds,
    std::vector<std::string>* traceDetails,
    std::string* errorText,
    LayoutGuideSheetRenderStats* stats) {
    return Render(
        snapshot,
        callouts,
        selectedCardIds,
        [&](int width, int height, SurfaceDrawCallback draw) {
            return dashboardRenderer_.RenderLayoutGuideSheetSurfaceOffscreen(width, height, draw);
        },
        traceDetails,
        errorText,
        stats);
}

bool LayoutGuideSheetRenderer::Render(const SystemSnapshot& snapshot,
    std::vector<LayoutGuideSheetCalloutRequest>& callouts,
    const std::vector<std::string>& selectedCardIds,
    const SurfaceRenderer& renderSurface,
    std::vector<std::string>* traceDetails,
    std::string* errorText,
    LayoutGuideSheetRenderStats* stats) {
    const auto recordStats = [&](std::chrono::nanoseconds LayoutGuideSheetRenderStats::* field,
                                 std::chrono::steady_clock::time_point start) {
        if (stats != nullptr) {
            (*stats).*field += std::chrono::steady_clock::now() - start;
        }
    };
    if (stats != nullptr) {
        *stats = {};
    }
    const auto measureStart = std::chrono::steady_clock::now();
    dashboardRenderer_.lastError_.clear();
    if (traceDetails != nullptr) {
        traceDetails->clear();
    }
    if (errorText != nullptr) {
        errorText->clear();
    }

    DashboardOverlayState overlayState;
    overlayState.showLayoutEditGuides = true;
    overlayState.forceLayoutEditAffordances = true;
    overlayState.forceHoverEquivalentAffordances = true;
    overlayState.hoverOnExposedDashboard = true;
    if (!dashboardRenderer_.PrimeLayoutEditDynamicRegions(snapshot, overlayState)) {
        if (errorText != nullptr) {
            *errorText = dashboardRenderer_.LastError();
        }
        recordStats(&LayoutGuideSheetRenderStats::measure, measureStart);
        return false;
    }

    using Callout = LayoutGuideSheetPlacementCallout;
    using CardPlacement = LayoutGuideSheetCardPlacement;
    std::vector<CardPlacement> cardPlacements;
    cardPlacements.reserve(selectedCardIds.size() + 1);
    const std::vector<LayoutGuideSheetCardSummary> cards = dashboardRenderer_.CollectLayoutGuideSheetCardSummaries();
    PackedOverview overview = BuildPackedOverview(dashboardRenderer_);
    for (PackedOverviewCard& card : overview.cards) {
        card.chromeArtifacts = dashboardRenderer_.BuildLayoutGuideSheetCardChromeArtifacts(card.id, card.rect, nullptr);
    }
    for (const std::string& selectedCardId : selectedCardIds) {
        const auto cardIt =
            std::find_if(cards.begin(), cards.end(), [&](const auto& card) { return card.id == selectedCardId; });
        if (cardIt == cards.end()) {
            continue;
        }
        cardPlacements.push_back(CardPlacement{cardIt->id, cardIt->rect, {}, false});
    }

    if (cardPlacements.empty()) {
        recordStats(&LayoutGuideSheetRenderStats::measure, measureStart);
        const auto drawStart = std::chrono::steady_clock::now();
        const bool saved = renderSurface(dashboardRenderer_.WindowWidth(), dashboardRenderer_.WindowHeight(), [&] {
            dashboardRenderer_.DrawFrame(snapshot, overlayState);
        });
        recordStats(&LayoutGuideSheetRenderStats::draw, drawStart);
        if (!saved && errorText != nullptr) {
            *errorText = dashboardRenderer_.LastError();
        }
        return saved;
    }
    StableSortCalloutsByPriority(callouts);

    const LayoutGuideSheetConfig& sheetStyle = dashboardRenderer_.Config().layout.layoutGuideSheet;
    const int sheetMargin = ScaleNonNegative(dashboardRenderer_, sheetStyle.sheetMargin);
    const int calloutGap = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutGap);
    const int bubblePaddingX = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutPaddingX);
    const int bubblePaddingY = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutPaddingY);
    const int lineGap = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutLineGap);
    const int bubbleRadius = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutRadius);
    const int leaderEndpointDiameter = ScaleNonNegative(dashboardRenderer_, sheetStyle.leaderEndpointDiameter);
    const int textLineHeight = std::max(1, dashboardRenderer_.Renderer().TextMetrics().smallText);
    const int targetSafeRadius = ScaleAtLeast(dashboardRenderer_, sheetStyle.leaderStrokeWidth + 2, 2);
    const int gaugeRingThickness =
        std::max(1, dashboardRenderer_.ScaleLogical(dashboardRenderer_.Config().layout.gauge.ringThickness));
    const auto measureCalloutBubble = [&](Callout& callout, int constrainedWidth = 0) {
        const bool constrained = constrainedWidth > 0;
        const int parameterWidth =
            std::max(1, dashboardRenderer_.Renderer().MeasureTextWidth(TextStyleId::Small, callout.parameterLine));
        const int descriptionWidth =
            callout.descriptionLine.empty()
                ? 0
                : std::max(
                      1, dashboardRenderer_.Renderer().MeasureTextWidth(TextStyleId::Small, callout.descriptionLine));
        const int contentWidth = std::max(parameterWidth, descriptionWidth);
        const int bubbleWidth = constrained ? constrainedWidth : contentWidth + bubblePaddingX * 2;
        const int constrainedContentWidth = std::max(1, bubbleWidth - bubblePaddingX * 2);
        int descriptionHeight = callout.descriptionLine.empty() ? 0 : textLineHeight;
        if (constrained && !callout.descriptionLine.empty()) {
            const TextLayoutResult wrappedDescription =
                dashboardRenderer_.Renderer().MeasureTextBlock(RenderRect{0, 0, constrainedContentWidth, 10000},
                    callout.descriptionLine,
                    TextStyleId::Small,
                    TextLayoutOptions::Wrapped(TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, false));
            descriptionHeight = std::max(textLineHeight, wrappedDescription.textRect.Height());
        }
        const int bubbleHeight =
            bubblePaddingY * 2 + textLineHeight + (callout.descriptionLine.empty() ? 0 : lineGap + descriptionHeight);
        callout.bubbleRect = RenderRect{0, 0, bubbleWidth, bubbleHeight};
        callout.wrapDescription = constrained;
    };
    for (Callout& callout : callouts) {
        measureCalloutBubble(callout);
    }

    cardPlacements.insert(
        cardPlacements.begin(), CardPlacement{kLayoutGuideSheetOverviewSourceId, overview.rect, {}, true});
    for (Callout& callout : callouts) {
        if (callout.sourceCardId != kLayoutGuideSheetOverviewSourceId) {
            continue;
        }
        if (callout.hoverLayoutGuide.has_value() && callout.hoverLayoutGuide->renderCardId.empty()) {
            const auto guideIt =
                std::find_if(overview.guides.begin(), overview.guides.end(), [&](const LayoutEditGuide& guide) {
                    return SameLayoutGuideIdentity(guide, *callout.hoverLayoutGuide);
                });
            if (guideIt != overview.guides.end()) {
                callout.targetRect = guideIt->lineRect;
            }
            continue;
        }
        if (callout.hoverGapAnchorKey.has_value() &&
            callout.hoverGapAnchorKey->widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome) {
            const auto anchorIt = std::find_if(
                overview.gapAnchors.begin(), overview.gapAnchors.end(), [&](const LayoutEditGapAnchor& anchor) {
                    return SameGapAnchorIdentity(anchor, *callout.hoverGapAnchorKey);
                });
            if (anchorIt != overview.gapAnchors.end()) {
                callout.targetRect = anchorIt->handleRect;
                callout.hoverGapAnchor = *anchorIt;
            }
            continue;
        }
        if (callout.hoverAnchorKey.has_value() &&
            callout.hoverAnchorKey->widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome) {
            const auto anchorIt = std::find_if(overview.reorderAnchors.begin(),
                overview.reorderAnchors.end(),
                [&](const LayoutEditAnchorRegion& anchor) {
                    return SameEditableAnchorIdentity(anchor, *callout.hoverAnchorKey);
                });
            if (anchorIt != overview.reorderAnchors.end()) {
                callout.hoverArtifactTargetRect =
                    anchorIt->targetRect.IsEmpty() ? anchorIt->anchorRect : anchorIt->targetRect;
                callout.targetRect = anchorIt->anchorRect;
                callout.hoverAnchorRect = anchorIt->anchorRect;
                callout.hoverAnchorDrawTargetOutline = anchorIt->drawTargetOutline && !anchorIt->targetRect.IsEmpty();
                callout.hoverAnchorShape = anchorIt->shape;
            }
            continue;
        }
        const std::string* cardId = nullptr;
        if (callout.hoverAnchorKey.has_value() &&
            callout.hoverAnchorKey->widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            cardId = &callout.hoverAnchorKey->widget.renderCardId;
        } else if (callout.hoverWidgetGuide.has_value() &&
                   callout.hoverWidgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            cardId = &callout.hoverWidgetGuide->widget.renderCardId;
        } else if (callout.hoverColorParameter.has_value()) {
            const auto cardIt = std::find_if(cards.begin(), cards.end(), [&](const LayoutGuideSheetCardSummary& card) {
                const bool overlapsTitle = card.chromeLayout.titleRect.IsEmpty() == false &&
                                           callout.targetRect.left < card.chromeLayout.titleRect.right &&
                                           callout.targetRect.right > card.chromeLayout.titleRect.left &&
                                           callout.targetRect.top < card.chromeLayout.titleRect.bottom &&
                                           callout.targetRect.bottom > card.chromeLayout.titleRect.top;
                const bool overlapsIcon = card.chromeLayout.iconRect.IsEmpty() == false &&
                                          callout.targetRect.left < card.chromeLayout.iconRect.right &&
                                          callout.targetRect.right > card.chromeLayout.iconRect.left &&
                                          callout.targetRect.top < card.chromeLayout.iconRect.bottom &&
                                          callout.targetRect.bottom > card.chromeLayout.iconRect.top;
                return card.chromeLayout.hasHeader && (overlapsTitle || overlapsIcon);
            });
            if (cardIt != cards.end()) {
                cardId = &cardIt->id;
            }
        } else {
            const auto cardIt = std::find_if(cards.begin(), cards.end(), [&](const LayoutGuideSheetCardSummary& card) {
                return card.chromeLayout.hasHeader && card.chromeLayout.titleRect.IsEmpty() == false &&
                       callout.targetRect.left < card.chromeLayout.titleRect.right &&
                       callout.targetRect.right > card.chromeLayout.titleRect.left &&
                       callout.targetRect.top < card.chromeLayout.titleRect.bottom &&
                       callout.targetRect.bottom > card.chromeLayout.titleRect.top;
            });
            if (cardIt != cards.end()) {
                cardId = &cardIt->id;
            }
        }
        if (cardId != nullptr) {
            const auto sourceCard =
                std::find_if(cards.begin(), cards.end(), [&](const auto& card) { return card.id == *cardId; });
            const auto packedCard = std::find_if(
                overview.cards.begin(), overview.cards.end(), [&](const auto& card) { return card.id == *cardId; });
            if (sourceCard != cards.end() && packedCard != overview.cards.end()) {
                if (callout.hoverAnchorKey.has_value()) {
                    const auto anchorIt = std::find_if(packedCard->chromeArtifacts.anchorRegions.begin(),
                        packedCard->chromeArtifacts.anchorRegions.end(),
                        [&](const LayoutEditAnchorRegion& region) {
                            return MatchesEditableAnchorKey(region.key, *callout.hoverAnchorKey);
                        });
                    if (anchorIt != packedCard->chromeArtifacts.anchorRegions.end()) {
                        callout.hoverArtifactTargetRect =
                            anchorIt->targetRect.IsEmpty() ? anchorIt->anchorRect : anchorIt->targetRect;
                        callout.targetRect = anchorIt->anchorRect;
                        callout.hoverAnchorRect = anchorIt->anchorRect;
                        callout.hoverAnchorDrawTargetOutline =
                            anchorIt->drawTargetOutline && !anchorIt->targetRect.IsEmpty();
                        callout.hoverAnchorShape = anchorIt->shape;
                    } else {
                        callout.targetRect = TransformRect(callout.targetRect, sourceCard->rect, packedCard->rect);
                    }
                } else if (callout.hoverWidgetGuide.has_value()) {
                    const auto guideIt = std::find_if(packedCard->chromeArtifacts.widgetGuides.begin(),
                        packedCard->chromeArtifacts.widgetGuides.end(),
                        [&](const LayoutEditWidgetGuide& guide) {
                            return MatchesWidgetEditGuide(guide, *callout.hoverWidgetGuide);
                        });
                    if (guideIt != packedCard->chromeArtifacts.widgetGuides.end()) {
                        callout.targetRect = guideIt->hitRect;
                    } else {
                        callout.targetRect = TransformRect(callout.targetRect, sourceCard->rect, packedCard->rect);
                    }
                } else if (callout.hoverColorParameter.has_value()) {
                    const auto colorIt = std::find_if(packedCard->chromeArtifacts.colorRegions.begin(),
                        packedCard->chromeArtifacts.colorRegions.end(),
                        [&](const LayoutEditColorRegion& region) {
                            return region.parameter == *callout.hoverColorParameter;
                        });
                    if (colorIt != packedCard->chromeArtifacts.colorRegions.end()) {
                        callout.targetRect = colorIt->targetRect;
                    } else {
                        callout.targetRect = TransformRect(callout.targetRect, sourceCard->rect, packedCard->rect);
                    }
                } else {
                    callout.targetRect = TransformRect(callout.targetRect, sourceCard->rect, packedCard->rect);
                }
            }
        }
    }

    for (Callout& callout : callouts) {
        if (callout.sourceCardId == kLayoutGuideSheetOverviewSourceId || !callout.hoverAnchorKey.has_value()) {
            continue;
        }
        const LayoutEditAnchorRegion* anchorRegion =
            dashboardRenderer_.FindEditableAnchorRegion(*callout.hoverAnchorKey);
        if (anchorRegion != nullptr) {
            callout.targetRect = anchorRegion->anchorRect;
            callout.hoverAnchorRect = anchorRegion->anchorRect;
            callout.targetAttachmentOnAnchorCircle =
                anchorRegion->shape == AnchorShape::Circle && anchorRegion->dragMode == AnchorDragMode::RadialDistance;
        }
    }
    recordStats(&LayoutGuideSheetRenderStats::measure, measureStart);

    const LayoutGuideSheetPlacementStyle placementStyle{sheetMargin,
        calloutGap,
        ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutRowGap),
        ScaleNonNegative(dashboardRenderer_, sheetStyle.blockGap),
        targetSafeRadius,
        gaugeRingThickness};
    const auto placementStart = std::chrono::steady_clock::now();
    const LayoutGuideSheetPlacementResult placementResult = PlaceLayoutGuideSheetCallouts(
        cardPlacements,
        callouts,
        placementStyle,
        [&](Callout& callout, int width) { measureCalloutBubble(callout, width); },
        traceDetails);
    recordStats(&LayoutGuideSheetRenderStats::placement, placementStart);
    const int sheetWidth = placementResult.sheetWidth;
    const int sheetHeight = placementResult.sheetHeight;

    const auto drawStart = std::chrono::steady_clock::now();

    struct RenderModeScope {
        DashboardRenderer& renderer;
        RenderMode previous;

        ~RenderModeScope() {
            renderer.renderMode_ = previous;
        }
    };

    const RenderModeScope renderModeScope{dashboardRenderer_, dashboardRenderer_.renderMode_};
    dashboardRenderer_.renderMode_ = RenderMode::LayoutGuideSheet;
    const bool saved = renderSurface(sheetWidth, sheetHeight, [&] {
        dashboardRenderer_.Renderer().FillSolidRect(
            RenderRect{0, 0, sheetWidth, sheetHeight}, RenderColorId::Background);
        dashboardRenderer_.BeginLayoutGuideSheetDynamicArtifacts(overlayState);
        const MetricSource& metrics = dashboardRenderer_.ResolveMetrics(snapshot);
        for (const CardPlacement& placement : cardPlacements) {
            if (placement.overview) {
                dashboardRenderer_.Renderer().FillSolidRect(placement.destRect, RenderColorId::Background);
                dashboardRenderer_.Renderer().DrawSolidRect(placement.destRect,
                    RenderStroke::Solid(RenderColorId::PanelBorder,
                        static_cast<float>(ScaleAtLeast(dashboardRenderer_, sheetStyle.overviewBorderWidth, 1))));
                for (const PackedOverviewCard& card : overview.cards) {
                    const RenderRect cardRect = TransformRect(card.rect, placement.sourceRect, placement.destRect);
                    dashboardRenderer_.BuildLayoutGuideSheetCardChromeArtifacts(card.id, cardRect, &metrics);
                }
                continue;
            }
            dashboardRenderer_.DrawLayoutGuideSheetCard(
                placement.id, placement.sourceRect, placement.destRect, metrics);
        }
        dashboardRenderer_.ResolveLayoutGuideSheetDynamicArtifactCollisions();
        DashboardOverlayState drawOverlayState;
        drawOverlayState.showLayoutEditGuides = true;
        drawOverlayState.forceLayoutEditAffordances = true;
        drawOverlayState.forceHoverEquivalentAffordances = true;
        drawOverlayState.drawExposedDashboardChrome = false;
        drawOverlayState.suppressLayoutGuideContainerHighlights = true;
        for (const CardPlacement& placement : cardPlacements) {
            if (placement.overview) {
                for (const Callout& callout : callouts) {
                    if (callout.sourceCardId != placement.id) {
                        continue;
                    }
                    if (callout.hoverColorParameter.has_value()) {
                        continue;
                    }
                    DrawOverviewArtifact(dashboardRenderer_, callout, placement.sourceRect, placement.destRect);
                }
                continue;
            }
            drawOverlayState.hoveredEditableAnchor.reset();
            drawOverlayState.hoveredEditableWidget.reset();
            drawOverlayState.hoveredLayoutEditGuide.reset();
            drawOverlayState.hoveredGapEditAnchor.reset();
            dashboardRenderer_.DrawLayoutGuideSheetOverlay(
                drawOverlayState, placement.sourceRect, placement.destRect, metrics);
            for (const Callout& callout : callouts) {
                if (callout.sourceCardId != placement.id) {
                    continue;
                }
                drawOverlayState.hoveredEditableAnchor = callout.hoverAnchorKey;
                drawOverlayState.hoveredEditableWidget.reset();
                if (callout.hoverAnchorKey.has_value() &&
                    callout.hoverAnchorKey->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
                    drawOverlayState.hoveredEditableWidget = callout.hoverAnchorKey->widget;
                }
                if (callout.hoverWidgetGuide.has_value()) {
                    drawOverlayState.hoveredEditableWidget = callout.hoverWidgetGuide->widget;
                }
                drawOverlayState.hoveredLayoutEditGuide = callout.hoverLayoutGuide;
                drawOverlayState.hoveredGapEditAnchor = callout.hoverGapAnchorKey;
                dashboardRenderer_.DrawLayoutGuideSheetOverlay(
                    drawOverlayState, placement.sourceRect, placement.destRect, metrics);
            }
        }
        dashboardRenderer_.EndLayoutGuideSheetDynamicArtifacts();
        for (const Callout& callout : callouts) {
            dashboardRenderer_.Renderer().DrawSolidLine(callout.targetAttachment,
                callout.bubbleAttachment,
                RenderStroke::Solid(RenderColorId::LayoutGuideCalloutLeader,
                    static_cast<float>(ScaleAtLeast(dashboardRenderer_, sheetStyle.leaderStrokeWidth, 1))));
        }
        for (const Callout& callout : callouts) {
            dashboardRenderer_.Renderer().FillSolidRoundedRect(
                callout.bubbleRect, bubbleRadius, RenderColorId::LayoutGuideCalloutFill);
            dashboardRenderer_.Renderer().DrawSolidRoundedRect(callout.bubbleRect,
                bubbleRadius,
                RenderStroke::Solid(RenderColorId::LayoutGuideCalloutBorder,
                    static_cast<float>(ScaleAtLeast(dashboardRenderer_, sheetStyle.calloutBorderWidth, 1))));
            if (leaderEndpointDiameter > 0) {
                dashboardRenderer_.Renderer().FillSolidEllipse(
                    CenteredSquare(callout.bubbleAttachment, leaderEndpointDiameter),
                    RenderColorId::LayoutGuideCalloutLeader);
            }
            const RenderRect textRect{callout.bubbleRect.left + bubblePaddingX,
                callout.bubbleRect.top + bubblePaddingY,
                callout.bubbleRect.right - bubblePaddingX,
                callout.bubbleRect.bottom - bubblePaddingY};
            const RenderRect parameterRect{textRect.left, textRect.top, textRect.right, textRect.top + textLineHeight};
            dashboardRenderer_.Renderer().DrawTextBlock(parameterRect,
                callout.parameterLine,
                TextStyleId::Small,
                RenderColorId::LayoutGuideCalloutParameter,
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, true));
            if (!callout.descriptionLine.empty()) {
                const RenderRect descriptionRect{
                    textRect.left, parameterRect.bottom + lineGap, textRect.right, textRect.bottom};
                dashboardRenderer_.Renderer().DrawTextBlock(descriptionRect,
                    callout.descriptionLine,
                    TextStyleId::Small,
                    RenderColorId::LayoutGuideCalloutDescription,
                    callout.wrapDescription
                        ? TextLayoutOptions::Wrapped(TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, false)
                        : TextLayoutOptions::SingleLine(
                              TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, true));
            }
        }
    });
    recordStats(&LayoutGuideSheetRenderStats::draw, drawStart);
    if (!dashboardRenderer_.renderer_->LastError().empty()) {
        dashboardRenderer_.lastError_ = dashboardRenderer_.renderer_->LastError();
    }
    if (!saved && errorText != nullptr) {
        *errorText = dashboardRenderer_.LastError();
    }
    if (saved && traceDetails != nullptr) {
        traceDetails->push_back(FormatText("canvas=\"%dx%d\"", sheetWidth, sheetHeight));
        std::string selectedCards = "cards=\"";
        for (size_t i = 0; i < cardPlacements.size(); ++i) {
            if (i > 0) {
                AppendFormat(selectedCards, ",");
            }
            AppendFormat(selectedCards, "%s", cardPlacements[i].id.c_str());
        }
        AppendFormat(selectedCards, "\"");
        traceDetails->push_back(selectedCards);
        traceDetails->push_back(FormatText("callouts=%zu", callouts.size()));
    }
    return saved;
}
