#include "layout_guide_sheet/layout_guide_sheet_renderer.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_model/layout_edit_helpers.h"

namespace {

long long Cross(RenderPoint a, RenderPoint b, RenderPoint c) {
    return static_cast<long long>(b.x - a.x) * static_cast<long long>(c.y - a.y) -
           static_cast<long long>(b.y - a.y) * static_cast<long long>(c.x - a.x);
}

bool PointsEqual(RenderPoint lhs, RenderPoint rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

RenderRect MakeOverviewSquareAnchorRect(int centerX, int centerY, int size) {
    const int half = size / 2;
    return RenderRect{centerX - half, centerY - half, centerX - half + size, centerY - half + size};
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

enum class RectExitSide {
    Left,
    Right,
};

struct PackedOverviewCard {
    std::string id;
    std::string title;
    std::string iconName;
    RenderRect rect{};
    RenderRect iconRect{};
    RenderRect titleRect{};
    LayoutGuideSheetCardChromeArtifacts chromeArtifacts;
};

struct PackedOverview {
    RenderRect rect{};
    std::vector<PackedOverviewCard> cards;
    std::vector<LayoutEditGuide> guides;
    std::vector<LayoutEditGapAnchor> gapAnchors;
};

struct PackedNode {
    int width = 0;
    int height = 0;
};

struct OverviewArtifact {
    RenderRect target{};
    std::optional<RenderRect> anchorRect;
    std::optional<LayoutEditWidgetGuide> widgetGuide;
    std::optional<LayoutEditGuide> layoutGuide;
    std::optional<LayoutEditGapAnchorKey> gapAnchorKey;
    std::optional<LayoutEditAnchorKey> anchorKey;
    std::optional<AnchorShape> anchorShape;
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
    const int hitInset = std::max(3, renderer.ScaleLogical(4));
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
        const int handleSize = std::max(4, renderer.ScaleLogical(6));
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
        guide.childFixedExtents.assign(childRects.size(), false);
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
        packedCard.title = card->title;
        packedCard.iconName = card->icon;
        packedCard.rect = rect;
        const CardChromeLayout chrome =
            ResolveCardChromeLayout(*card, packedCard.rect, ResolveCardChromeLayoutMetrics(renderer));
        packedCard.iconRect = chrome.iconRect;
        packedCard.titleRect = chrome.titleRect;
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
        std::vector<size_t> childPath = nodePath;
        childPath.push_back(i);
        AppendPackedCards(node.children[i], renderer, childRect, childPath, overview);
        cursor += extent + gap;
        remainingExtra -= extra;
        remainingWeight -= weight;
    }
    AddPackedDashboardGuides(overview, renderer, node, rect, childRects, gap, nodePath);
}

PackedOverview BuildPackedOverview(DashboardRenderer& renderer) {
    PackedNode root = MeasurePackedNode(renderer.Config().layout.structure.cardsLayout, renderer);
    const int outerMargin = renderer.ScaleLogical(renderer.Config().layout.dashboard.outerMargin);
    PackedOverview overview;
    overview.rect = RenderRect{0, 0, root.width + outerMargin * 2, root.height + outerMargin * 2};
    AppendPackedCards(renderer.Config().layout.structure.cardsLayout,
        renderer,
        RenderRect{outerMargin, outerMargin, outerMargin + root.width, outerMargin + root.height},
        {},
        overview);
    return overview;
}

void DrawDottedOverviewRect(DashboardRenderer& renderer, const RenderRect& rect) {
    if (rect.IsEmpty()) {
        return;
    }
    const int padding = std::max(1, renderer.ScaleLogical(1));
    const RenderRect drawRect = rect.Inflate(padding, padding);
    const int strokeWidth = std::max(1, renderer.ScaleLogical(1));
    const int dotLength = std::max(strokeWidth + 1, renderer.ScaleLogical(5));
    const int gapLength = std::max(strokeWidth + 1, renderer.ScaleLogical(4));
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
    const OverviewArtifact& artifact,
    const RenderRect& sourceRect,
    const RenderRect& destRect) {
    const RenderRect target = TransformRect(artifact.target, sourceRect, destRect);
    const std::optional<RenderRect> anchor =
        artifact.anchorRect.has_value()
            ? std::optional<RenderRect>(TransformRect(*artifact.anchorRect, sourceRect, destRect))
            : std::nullopt;
    const RenderPoint center = target.Center();
    const auto drawGuideLine = [&](LayoutGuideAxis axis) {
        if (axis == LayoutGuideAxis::Vertical) {
            renderer.Renderer().DrawSolidLine(RenderPoint{center.x, target.top},
                RenderPoint{center.x, target.bottom},
                RenderStroke::Solid(RenderColorId::LayoutGuide, 1.0f));
        } else {
            renderer.Renderer().DrawSolidLine(RenderPoint{target.left, center.y},
                RenderPoint{target.right, center.y},
                RenderStroke::Solid(RenderColorId::LayoutGuide, 1.0f));
        }
    };
    if (artifact.layoutGuide.has_value()) {
        drawGuideLine(artifact.layoutGuide->axis);
        return;
    }
    if (artifact.widgetGuide.has_value()) {
        drawGuideLine(artifact.widgetGuide->axis);
        return;
    }
    if (artifact.gapAnchorKey.has_value()) {
        const int handleSize = std::max(4, renderer.ScaleLogical(6));
        const RenderRect handle{center.x - handleSize / 2,
            center.y - handleSize / 2,
            center.x - handleSize / 2 + handleSize,
            center.y - handleSize / 2 + handleSize};
        renderer.Renderer().FillSolidRect(handle, RenderColorId::ActiveEdit);
        return;
    }
    if (artifact.anchorKey.has_value()) {
        DrawDottedOverviewRect(renderer, target);
        const int size = std::max(4, std::min(std::max(target.Width(), target.Height()), renderer.ScaleLogical(10)));
        const RenderRect handle = anchor.value_or(RenderRect{
            center.x - size / 2, center.y - size / 2, center.x - size / 2 + size, center.y - size / 2 + size});
        const AnchorShape shape = artifact.anchorShape.value_or(AnchorShape::Circle);
        if (shape == AnchorShape::Wedge) {
            const float outlineWidth = static_cast<float>(std::max(1, renderer.ScaleLogical(1)));
            const RenderPoint topRight{handle.right, handle.top};
            const RenderPoint bottomLeft{handle.left, handle.bottom};
            const RenderPoint bottomRight{handle.right, handle.bottom};
            renderer.Renderer().DrawSolidLine(
                bottomLeft, bottomRight, RenderStroke::Solid(RenderColorId::LayoutGuide, outlineWidth));
            renderer.Renderer().DrawSolidLine(
                topRight, bottomRight, RenderStroke::Solid(RenderColorId::LayoutGuide, outlineWidth));
        } else if (shape == AnchorShape::Diamond) {
            renderer.Renderer().FillSolidDiamond(handle, RenderColorId::ActiveEdit);
        } else if (shape == AnchorShape::Square || shape == AnchorShape::VerticalReorder ||
                   shape == AnchorShape::HorizontalReorder || shape == AnchorShape::Plus) {
            renderer.Renderer().FillSolidRect(handle, RenderColorId::ActiveEdit);
        } else {
            renderer.Renderer().FillSolidEllipse(handle, RenderColorId::ActiveEdit);
        }
        return;
    }
    renderer.Renderer().DrawSolidRect(target, RenderStroke::Solid(RenderColorId::LayoutGuide, 1.0f));
}

}  // namespace

LayoutGuideSheetRenderer::LayoutGuideSheetRenderer(DashboardRenderer& dashboardRenderer)
    : dashboardRenderer_(dashboardRenderer) {}

bool LayoutGuideSheetRenderer::SavePng(const std::filesystem::path& imagePath,
    const SystemSnapshot& snapshot,
    const std::vector<LayoutGuideSheetCalloutRequest>& calloutRequests,
    const std::vector<std::string>& selectedCardIds,
    std::vector<std::string>* traceDetails,
    std::string* errorText) {
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
    overlayState.hoverOnExposedDashboard = true;
    if (!dashboardRenderer_.PrimeLayoutEditDynamicRegions(snapshot, overlayState)) {
        if (errorText != nullptr) {
            *errorText = dashboardRenderer_.LastError();
        }
        return false;
    }

    struct Callout {
        std::string key;
        std::string sourceCardId;
        std::string parameterLine;
        std::string descriptionLine;
        std::optional<LayoutEditAnchorKey> hoverAnchorKey;
        std::optional<LayoutEditWidgetGuide> hoverWidgetGuide;
        std::optional<LayoutEditGuide> hoverLayoutGuide;
        std::optional<LayoutEditGapAnchorKey> hoverGapAnchorKey;
        std::optional<AnchorShape> hoverAnchorShape;
        RenderRect targetRect{};
        std::optional<RenderRect> hoverAnchorRect;
        RenderRect bubbleRect{};
        RenderPoint targetAttachment{};
        RenderPoint bubbleAttachment{};
        RectExitSide exitSide = RectExitSide::Right;
        int priority = 1000;
        size_t order = 0;
    };

    std::vector<Callout> callouts;
    callouts.reserve(calloutRequests.size());
    for (const LayoutGuideSheetCalloutRequest& request : calloutRequests) {
        callouts.push_back(Callout{request.key,
            request.sourceCardId,
            request.parameterLine,
            request.descriptionLine,
            request.hoverAnchorKey,
            request.hoverWidgetGuide,
            request.hoverLayoutGuide,
            request.hoverGapAnchorKey,
            request.hoverAnchorShape,
            request.targetRect,
            std::nullopt,
            {},
            {},
            {},
            RectExitSide::Right,
            request.priority,
            request.order});
    }

    struct CardPlacement {
        std::string id;
        RenderRect sourceRect{};
        RenderRect destRect{};
        bool overview = false;
    };

    std::vector<CardPlacement> cardPlacements;
    cardPlacements.reserve(selectedCardIds.size() + 1);
    int cardColumnWidth = 0;
    const std::vector<LayoutGuideSheetCardSummary> cards = dashboardRenderer_.CollectLayoutGuideSheetCardSummaries();
    PackedOverview overview = BuildPackedOverview(dashboardRenderer_);
    for (PackedOverviewCard& card : overview.cards) {
        card.chromeArtifacts = dashboardRenderer_.BuildLayoutGuideSheetCardChromeArtifacts(card.id, card.rect, nullptr);
        card.iconRect = card.chromeArtifacts.chromeLayout.iconRect;
        card.titleRect = card.chromeArtifacts.chromeLayout.titleRect;
    }
    for (const std::string& selectedCardId : selectedCardIds) {
        const auto cardIt =
            std::find_if(cards.begin(), cards.end(), [&](const auto& card) { return card.id == selectedCardId; });
        if (cardIt == cards.end()) {
            continue;
        }
        cardPlacements.push_back(CardPlacement{cardIt->id, cardIt->rect, {}, false});
        cardColumnWidth = std::max(cardColumnWidth, cardIt->rect.Width());
    }

    if (cardPlacements.empty()) {
        const bool saved = dashboardRenderer_.SaveSnapshotPng(imagePath, snapshot, overlayState);
        if (!saved && errorText != nullptr) {
            *errorText = dashboardRenderer_.LastError();
        }
        return saved;
    }
    std::stable_sort(callouts.begin(), callouts.end(), [](const Callout& lhs, const Callout& rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority < rhs.priority;
        }
        return lhs.order < rhs.order;
    });

    const int sheetMargin = dashboardRenderer_.ScaleLogical(48);
    const int calloutGap = dashboardRenderer_.ScaleLogical(48);
    const int bubblePaddingX = dashboardRenderer_.ScaleLogical(8);
    const int bubblePaddingY = dashboardRenderer_.ScaleLogical(6);
    const int lineGap = dashboardRenderer_.ScaleLogical(3);
    const int bubbleRadius = dashboardRenderer_.ScaleLogical(4);
    const int maxBubbleWidth = dashboardRenderer_.ScaleLogical(860);
    const int minBubbleWidth = dashboardRenderer_.ScaleLogical(180);
    const int textLineHeight = std::max(1, dashboardRenderer_.Renderer().TextMetrics().smallText);
    int maxBubbleMeasuredWidth = minBubbleWidth;
    for (Callout& callout : callouts) {
        const int parameterWidth =
            std::max(1, dashboardRenderer_.Renderer().MeasureTextWidth(TextStyleId::Small, callout.parameterLine));
        const int descriptionWidth =
            callout.descriptionLine.empty()
                ? 0
                : std::max(
                      1, dashboardRenderer_.Renderer().MeasureTextWidth(TextStyleId::Small, callout.descriptionLine));
        const int contentWidth = std::max(parameterWidth, descriptionWidth);
        const int bubbleWidth = std::clamp(contentWidth + bubblePaddingX * 2, minBubbleWidth, maxBubbleWidth);
        const int bubbleHeight =
            bubblePaddingY * 2 + textLineHeight + (callout.descriptionLine.empty() ? 0 : lineGap + textLineHeight);
        callout.bubbleRect = RenderRect{0, 0, bubbleWidth, bubbleHeight};
        maxBubbleMeasuredWidth = std::max(maxBubbleMeasuredWidth, bubbleWidth);
    }

    const int contentColumnWidth = std::max(cardColumnWidth, overview.rect.Width());
    const int overviewWidth = overview.rect.Width();
    const int overviewHeight = overview.rect.Height();
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
            }
            continue;
        }
        std::optional<std::string> cardId;
        if (callout.hoverAnchorKey.has_value() &&
            callout.hoverAnchorKey->widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            cardId = callout.hoverAnchorKey->widget.renderCardId;
        } else if (callout.hoverWidgetGuide.has_value() &&
                   callout.hoverWidgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            cardId = callout.hoverWidgetGuide->widget.renderCardId;
        } else {
            const auto cardIt = std::find_if(cards.begin(), cards.end(), [&](const LayoutGuideSheetCardSummary& card) {
                return card.chromeLayout.hasHeader && card.chromeLayout.titleRect.IsEmpty() == false &&
                       callout.targetRect.left < card.chromeLayout.titleRect.right &&
                       callout.targetRect.right > card.chromeLayout.titleRect.left &&
                       callout.targetRect.top < card.chromeLayout.titleRect.bottom &&
                       callout.targetRect.bottom > card.chromeLayout.titleRect.top;
            });
            if (cardIt != cards.end()) {
                cardId = cardIt->id;
            }
        }
        if (cardId.has_value()) {
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
                        callout.targetRect = anchorIt->targetRect;
                        callout.hoverAnchorRect = anchorIt->anchorRect;
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
                } else {
                    callout.targetRect = TransformRect(callout.targetRect, sourceCard->rect, packedCard->rect);
                }
            }
        } else {
            callout.targetRect = TransformRect(callout.targetRect,
                RenderRect{0, 0, dashboardRenderer_.WindowWidth(), dashboardRenderer_.WindowHeight()},
                overview.rect);
        }
    }

    const int cardColumnX = sheetMargin + maxBubbleMeasuredWidth + calloutGap;
    const int cardColumnY = sheetMargin;
    const int cardGap = dashboardRenderer_.ScaleLogical(72);
    const int rightColumnX = cardColumnX + contentColumnWidth + calloutGap;
    const int sheetWidth = rightColumnX + maxBubbleMeasuredWidth + sheetMargin;
    int sheetHeight = cardColumnY + dashboardRenderer_.WindowHeight() + sheetMargin;

    struct PlannedCallout {
        size_t calloutIndex = 0;
        size_t cardIndex = 0;
        RenderRect target{};
    };

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
        const Callout& callout = callouts[i];
        const size_t calloutCardOrder = cardOrder(callout.sourceCardId);
        if (calloutCardOrder >= cardPlacements.size()) {
            continue;
        }
        plannedCallouts.push_back(PlannedCallout{i, calloutCardOrder, callout.targetRect});
    }

    const int rowGap = std::max(dashboardRenderer_.ScaleLogical(12), 1);

    struct CardCalloutColumns {
        std::vector<size_t> left;
        std::vector<size_t> right;
    };

    std::vector<CardCalloutColumns> plannedByCard(cardPlacements.size());
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        std::vector<size_t> cardPlanned;
        for (size_t plannedIndex = 0; plannedIndex < plannedCallouts.size(); ++plannedIndex) {
            if (plannedCallouts[plannedIndex].cardIndex == cardIndex) {
                cardPlanned.push_back(plannedIndex);
            }
        }
        std::stable_sort(cardPlanned.begin(), cardPlanned.end(), [&](size_t lhs, size_t rhs) {
            const RenderPoint lhsCenter = plannedCallouts[lhs].target.Center();
            const RenderPoint rhsCenter = plannedCallouts[rhs].target.Center();
            if (lhsCenter.x != rhsCenter.x) {
                return lhsCenter.x < rhsCenter.x;
            }
            if (lhsCenter.y != rhsCenter.y) {
                return lhsCenter.y < rhsCenter.y;
            }
            return callouts[plannedCallouts[lhs].calloutIndex].order <
                   callouts[plannedCallouts[rhs].calloutIndex].order;
        });
        const size_t leftCount = cardPlanned.size() == 1 ? (plannedCallouts[cardPlanned.front()].target.Center().x <
                                                                       cardPlacements[cardIndex].sourceRect.Center().x
                                                                   ? 1
                                                                   : 0)
                                                         : cardPlanned.size() / 2;
        plannedByCard[cardIndex].left.assign(cardPlanned.begin(), cardPlanned.begin() + leftCount);
        plannedByCard[cardIndex].right.assign(cardPlanned.begin() + leftCount, cardPlanned.end());
        const auto sortByTargetY = [&](std::vector<size_t>& plannedIndexes) {
            std::stable_sort(plannedIndexes.begin(), plannedIndexes.end(), [&](size_t lhs, size_t rhs) {
                const RenderPoint lhsCenter = plannedCallouts[lhs].target.Center();
                const RenderPoint rhsCenter = plannedCallouts[rhs].target.Center();
                if (lhsCenter.y != rhsCenter.y) {
                    return lhsCenter.y < rhsCenter.y;
                }
                if (lhsCenter.x != rhsCenter.x) {
                    return lhsCenter.x < rhsCenter.x;
                }
                return callouts[plannedCallouts[lhs].calloutIndex].order <
                       callouts[plannedCallouts[rhs].calloutIndex].order;
            });
        };
        sortByTargetY(plannedByCard[cardIndex].left);
        sortByTargetY(plannedByCard[cardIndex].right);
    }

    const auto stackedHeight = [&](const std::vector<size_t>& plannedIndexes) {
        int height = 0;
        for (size_t i = 0; i < plannedIndexes.size(); ++i) {
            height += callouts[plannedCallouts[plannedIndexes[i]].calloutIndex].bubbleRect.Height();
            if (i + 1 < plannedIndexes.size()) {
                height += rowGap;
            }
        }
        return height;
    };
    int cardCursorY = cardColumnY;
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        CardPlacement& placement = cardPlacements[cardIndex];
        const int placementHeight = placement.overview ? overviewHeight : placement.sourceRect.Height();
        const int placementWidth = placement.overview ? overviewWidth : placement.sourceRect.Width();
        const int blockHeight = std::max({placementHeight,
            stackedHeight(plannedByCard[cardIndex].left),
            stackedHeight(plannedByCard[cardIndex].right)});
        const int cardOffsetY = std::max(0, (blockHeight - placementHeight) / 2);
        placement.destRect = RenderRect{cardColumnX,
            cardCursorY + cardOffsetY,
            cardColumnX + placementWidth,
            cardCursorY + cardOffsetY + placementHeight};
        cardCursorY += blockHeight + cardGap;
    }
    sheetHeight = std::max(sheetHeight, cardCursorY - cardGap + sheetMargin);

    const int leftColumnRight = cardColumnX - calloutGap;
    const int rightColumnLeft = rightColumnX;
    const auto placeSide = [&](const std::vector<size_t>& plannedIndexes,
                               RectExitSide side,
                               const RenderRect& cardRect) {
        int y = cardRect.Center().y - stackedHeight(plannedIndexes) / 2;
        y = std::max(sheetMargin, y);
        for (const size_t plannedIndex : plannedIndexes) {
            const PlannedCallout& planned = plannedCallouts[plannedIndex];
            Callout& callout = callouts[planned.calloutIndex];
            const int x = side == RectExitSide::Left ? leftColumnRight - callout.bubbleRect.Width() : rightColumnLeft;
            callout.bubbleRect = RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
            callout.exitSide = side;
            const int dx = cardRect.left - cardPlacements[planned.cardIndex].sourceRect.left;
            const int dy = cardRect.top - cardPlacements[planned.cardIndex].sourceRect.top;
            callout.targetAttachment =
                cardPlacements[planned.cardIndex].overview
                    ? TransformPoint(planned.target.Center(),
                          cardPlacements[planned.cardIndex].sourceRect,
                          cardPlacements[planned.cardIndex].destRect)
                    : RenderPoint{planned.target.Center().x + dx, planned.target.Center().y + dy};
            const int attachmentY =
                std::clamp(callout.targetAttachment.y, callout.bubbleRect.top, callout.bubbleRect.bottom);
            callout.bubbleAttachment = RenderPoint{
                side == RectExitSide::Left ? callout.bubbleRect.right : callout.bubbleRect.left, attachmentY};
            y = callout.bubbleRect.bottom + rowGap;
            sheetHeight = std::max(sheetHeight, callout.bubbleRect.bottom + sheetMargin);
        }
    };

    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        const CardPlacement& placement = cardPlacements[cardIndex];
        placeSide(plannedByCard[cardIndex].left, RectExitSide::Left, placement.destRect);
        placeSide(plannedByCard[cardIndex].right, RectExitSide::Right, placement.destRect);
    }

    const auto validateSideOrder =
        [&](std::vector<size_t>& plannedIndexes, RectExitSide side, const RenderRect& cardRect) {
            if (plannedIndexes.size() < 2) {
                return;
            }
            bool swapped = true;
            size_t passCount = 0;
            while (swapped && passCount < plannedIndexes.size() * plannedIndexes.size()) {
                swapped = false;
                ++passCount;
                for (size_t i = 0; i < plannedIndexes.size(); ++i) {
                    for (size_t j = i + 1; j < plannedIndexes.size(); ++j) {
                        const Callout& previous = callouts[plannedCallouts[plannedIndexes[i]].calloutIndex];
                        const Callout& current = callouts[plannedCallouts[plannedIndexes[j]].calloutIndex];
                        if (LeaderSegmentsIntersect(previous.targetAttachment,
                                previous.bubbleAttachment,
                                current.targetAttachment,
                                current.bubbleAttachment)) {
                            std::swap(plannedIndexes[i], plannedIndexes[j]);
                            placeSide(plannedIndexes, side, cardRect);
                            swapped = true;
                        }
                    }
                }
            }
        };
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        validateSideOrder(plannedByCard[cardIndex].left, RectExitSide::Left, cardPlacements[cardIndex].destRect);
        validateSideOrder(plannedByCard[cardIndex].right, RectExitSide::Right, cardPlacements[cardIndex].destRect);
        placeSide(plannedByCard[cardIndex].left, RectExitSide::Left, cardPlacements[cardIndex].destRect);
        placeSide(plannedByCard[cardIndex].right, RectExitSide::Right, cardPlacements[cardIndex].destRect);
    }

    const auto sameSideLeaderIntersects = [&](const Callout& callout, const Callout& other) {
        if (callout.exitSide != other.exitSide || callout.sourceCardId != other.sourceCardId) {
            return false;
        }
        return LeaderSegmentsIntersect(
            callout.targetAttachment, callout.bubbleAttachment, other.targetAttachment, other.bubbleAttachment);
    };

    std::vector<const Callout*> leaders;
    for (const Callout& callout : callouts) {
        const auto crossingIt = std::find_if(leaders.begin(), leaders.end(), [&](const auto& leader) {
            return sameSideLeaderIntersects(callout, *leader);
        });
        if (crossingIt != leaders.end() && traceDetails != nullptr) {
            traceDetails->push_back("warning=\"leader_intersection_detected\" callout=\"" + callout.key + "\"");
        }
        leaders.push_back(&callout);
    }

    const bool saved = dashboardRenderer_.SaveLayoutGuideSheetSurfacePng(imagePath, sheetWidth, sheetHeight, [&] {
        dashboardRenderer_.Renderer().FillSolidRect(
            RenderRect{0, 0, sheetWidth, sheetHeight}, RenderColorId::Background);
        dashboardRenderer_.BeginLayoutGuideSheetDynamicArtifacts(overlayState);
        const MetricSource& metrics = dashboardRenderer_.ResolveMetrics(snapshot);
        for (const CardPlacement& placement : cardPlacements) {
            if (placement.overview) {
                dashboardRenderer_.Renderer().FillSolidRect(placement.destRect, RenderColorId::Background);
                dashboardRenderer_.Renderer().DrawSolidRect(
                    placement.destRect, RenderStroke::Solid(RenderColorId::PanelBorder, 1.0f));
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
        for (const CardPlacement& placement : cardPlacements) {
            if (placement.overview) {
                for (const Callout& callout : callouts) {
                    if (callout.sourceCardId != placement.id) {
                        continue;
                    }
                    DrawOverviewArtifact(dashboardRenderer_,
                        OverviewArtifact{callout.targetRect,
                            callout.hoverAnchorRect,
                            callout.hoverWidgetGuide,
                            callout.hoverLayoutGuide,
                            callout.hoverGapAnchorKey,
                            callout.hoverAnchorKey,
                            callout.hoverAnchorShape},
                        placement.sourceRect,
                        placement.destRect);
                }
                continue;
            }
            dashboardRenderer_.DrawLayoutGuideSheetOverlay(
                overlayState, placement.sourceRect, placement.destRect, metrics);
            for (const Callout& callout : callouts) {
                if (callout.sourceCardId != placement.id) {
                    continue;
                }
                DashboardOverlayState calloutOverlayState = overlayState;
                calloutOverlayState.hoverOnExposedDashboard = false;
                calloutOverlayState.hoveredEditableAnchor = callout.hoverAnchorKey;
                if (callout.hoverWidgetGuide.has_value()) {
                    calloutOverlayState.hoveredEditableWidget = callout.hoverWidgetGuide->widget;
                }
                calloutOverlayState.hoveredLayoutEditGuide = callout.hoverLayoutGuide;
                calloutOverlayState.hoveredGapEditAnchor = callout.hoverGapAnchorKey;
                dashboardRenderer_.DrawLayoutGuideSheetOverlay(
                    calloutOverlayState, placement.sourceRect, placement.destRect, metrics);
            }
        }
        dashboardRenderer_.EndLayoutGuideSheetDynamicArtifacts();
        for (const Callout& callout : callouts) {
            dashboardRenderer_.Renderer().DrawSolidLine(callout.targetAttachment,
                callout.bubbleAttachment,
                RenderStroke::Solid(RenderColorId::LayoutGuideCalloutLeader, 1.0f));
        }
        for (const Callout& callout : callouts) {
            dashboardRenderer_.Renderer().FillSolidRoundedRect(
                callout.bubbleRect, bubbleRadius, RenderColorId::LayoutGuideCalloutFill);
            dashboardRenderer_.Renderer().DrawSolidRoundedRect(
                callout.bubbleRect, bubbleRadius, RenderStroke::Solid(RenderColorId::LayoutGuideCalloutBorder, 1.0f));
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
                    TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Top, true, true));
            }
        }
    });
    if (!dashboardRenderer_.renderer_->LastError().empty()) {
        dashboardRenderer_.lastError_ = dashboardRenderer_.renderer_->LastError();
    }
    if (!saved && errorText != nullptr) {
        *errorText = dashboardRenderer_.LastError();
    }
    if (saved && traceDetails != nullptr) {
        traceDetails->push_back("canvas=\"" + std::to_string(sheetWidth) + "x" + std::to_string(sheetHeight) + "\"");
        std::string selectedCards = "cards=\"";
        for (size_t i = 0; i < cardPlacements.size(); ++i) {
            if (i > 0) {
                selectedCards += ",";
            }
            selectedCards += cardPlacements[i].id;
        }
        selectedCards += "\"";
        traceDetails->push_back(selectedCards);
        traceDetails->push_back("callouts=" + std::to_string(callouts.size()));
    }
    return saved;
}
