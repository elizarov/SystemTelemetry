#include "layout_guide_sheet/layout_guide_sheet_renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

int ScaleNonNegative(DashboardRenderer& renderer, int value) {
    return std::max(0, renderer.ScaleLogical(value));
}

int ScaleAtLeast(DashboardRenderer& renderer, int value, int minimum) {
    return std::max(minimum, renderer.ScaleLogical(value));
}

enum class RectExitSide {
    Left,
    Right,
    Top,
    Bottom,
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
    std::vector<LayoutEditAnchorRegion> reorderAnchors;
};

struct PackedNode {
    int width = 0;
    int height = 0;
};

struct OverviewArtifact {
    RenderRect target{};
    std::optional<RenderRect> anchorRect;
    std::optional<LayoutEditGapAnchor> gapAnchor;
    bool drawAnchorTargetOutline = true;
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
    const OverviewArtifact& artifact,
    const RenderRect& sourceRect,
    const RenderRect& destRect) {
    const RenderRect target = TransformRect(artifact.target, sourceRect, destRect);
    const LayoutGuideSheetConfig& sheetStyle = renderer.Config().layout.layoutGuideSheet;
    const std::optional<RenderRect> anchor =
        artifact.anchorRect.has_value()
            ? std::optional<RenderRect>(TransformRect(*artifact.anchorRect, sourceRect, destRect))
            : std::nullopt;
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
    if (artifact.layoutGuide.has_value()) {
        drawGuideLine(artifact.layoutGuide->axis);
        return;
    }
    if (artifact.widgetGuide.has_value()) {
        drawGuideLine(artifact.widgetGuide->axis);
        return;
    }
    if (artifact.gapAnchor.has_value()) {
        const LayoutEditGapAnchor& gapAnchor = *artifact.gapAnchor;
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
    if (artifact.anchorKey.has_value()) {
        if (artifact.drawAnchorTargetOutline) {
            DrawDottedOverviewRect(renderer, target);
        }
        const int size = std::max(1,
            std::min(std::max(target.Width(), target.Height()),
                ScaleAtLeast(renderer, sheetStyle.overviewAnchorMaxSize, 1)));
        const RenderRect handle = anchor.value_or(RenderRect{
            center.x - size / 2, center.y - size / 2, center.x - size / 2 + size, center.y - size / 2 + size});
        const AnchorShape shape = artifact.anchorShape.value_or(AnchorShape::Circle);
        if (shape == AnchorShape::Wedge) {
            const float outlineWidth =
                static_cast<float>(ScaleAtLeast(renderer, sheetStyle.overviewGuideStrokeWidth, 1));
            const RenderPoint topRight{handle.right, handle.top};
            const RenderPoint bottomLeft{handle.left, handle.bottom};
            const RenderPoint bottomRight{handle.right, handle.bottom};
            renderer.Renderer().DrawSolidLine(
                bottomLeft, bottomRight, RenderStroke::Solid(RenderColorId::LayoutGuide, outlineWidth));
            renderer.Renderer().DrawSolidLine(
                topRight, bottomRight, RenderStroke::Solid(RenderColorId::LayoutGuide, outlineWidth));
        } else if (shape == AnchorShape::Diamond) {
            renderer.Renderer().FillSolidDiamond(handle, RenderColorId::LayoutGuide);
        } else if (shape == AnchorShape::VerticalReorder || shape == AnchorShape::HorizontalReorder) {
            const int outlineWidth = ScaleAtLeast(renderer, sheetStyle.overviewGuideStrokeWidth, 1);
            const int centerX = handle.left + (std::max<LONG>(0, handle.right - handle.left) / 2);
            const int centerY = handle.top + (std::max<LONG>(0, handle.bottom - handle.top) / 2);
            const int gapHalf = ScaleAtLeast(renderer, 1, 1);
            const auto stroke = RenderStroke::Solid(RenderColorId::LayoutGuide, static_cast<float>(outlineWidth));
            if (shape == AnchorShape::HorizontalReorder) {
                const int halfHeight = std::max(1, static_cast<int>(handle.bottom - handle.top) / 2);
                const RenderPoint leftApex{handle.left, centerY};
                const RenderPoint leftTop{centerX - gapHalf, centerY - halfHeight};
                const RenderPoint leftBottom{centerX - gapHalf, centerY + halfHeight};
                const RenderPoint rightApex{handle.right, centerY};
                const RenderPoint rightTop{centerX + gapHalf, centerY - halfHeight};
                const RenderPoint rightBottom{centerX + gapHalf, centerY + halfHeight};
                renderer.Renderer().DrawSolidLine(leftApex, leftTop, stroke);
                renderer.Renderer().DrawSolidLine(leftTop, leftBottom, stroke);
                renderer.Renderer().DrawSolidLine(leftBottom, leftApex, stroke);
                renderer.Renderer().DrawSolidLine(rightTop, rightApex, stroke);
                renderer.Renderer().DrawSolidLine(rightApex, rightBottom, stroke);
                renderer.Renderer().DrawSolidLine(rightBottom, rightTop, stroke);
            } else {
                const int halfWidth = std::max(1, static_cast<int>(handle.right - handle.left) / 2);
                const RenderPoint upApex{centerX, handle.top};
                const RenderPoint upLeft{centerX - halfWidth, centerY - gapHalf};
                const RenderPoint upRight{centerX + halfWidth, centerY - gapHalf};
                const RenderPoint downApex{centerX, handle.bottom};
                const RenderPoint downLeft{centerX - halfWidth, centerY + gapHalf};
                const RenderPoint downRight{centerX + halfWidth, centerY + gapHalf};
                renderer.Renderer().DrawSolidLine(upApex, upLeft, stroke);
                renderer.Renderer().DrawSolidLine(upLeft, upRight, stroke);
                renderer.Renderer().DrawSolidLine(upRight, upApex, stroke);
                renderer.Renderer().DrawSolidLine(downLeft, downApex, stroke);
                renderer.Renderer().DrawSolidLine(downApex, downRight, stroke);
                renderer.Renderer().DrawSolidLine(downRight, downLeft, stroke);
            }
        } else if (shape == AnchorShape::Square || shape == AnchorShape::Plus) {
            renderer.Renderer().FillSolidRect(handle, RenderColorId::LayoutGuide);
        } else {
            renderer.Renderer().FillSolidEllipse(handle, RenderColorId::LayoutGuide);
        }
        return;
    }
    renderer.Renderer().DrawSolidRect(target,
        RenderStroke::Solid(RenderColorId::LayoutGuide,
            static_cast<float>(ScaleAtLeast(renderer, sheetStyle.overviewGuideStrokeWidth, 1))));
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
    overlayState.forceHoverEquivalentColors = true;
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
        std::optional<LayoutEditParameter> hoverColorParameter;
        RenderRect targetRect{};
        std::optional<RenderRect> hoverArtifactTargetRect;
        std::optional<RenderRect> hoverAnchorRect;
        std::optional<LayoutEditGapAnchor> hoverGapAnchor;
        bool hoverAnchorDrawTargetOutline = true;
        bool targetAttachmentOnAnchorCircle = false;
        RenderRect bubbleRect{};
        RenderPoint targetAttachment{};
        RenderPoint bubbleAttachment{};
        RectExitSide exitSide = RectExitSide::Right;
        int priority = 1000;
        size_t order = 0;
        bool wrapDescription = false;
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
            request.hoverColorParameter,
            request.targetRect,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            true,
            false,
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

    const LayoutGuideSheetConfig& sheetStyle = dashboardRenderer_.Config().layout.layoutGuideSheet;
    const int sheetMargin = ScaleNonNegative(dashboardRenderer_, sheetStyle.sheetMargin);
    const int calloutGap = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutGap);
    const int bubblePaddingX = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutPaddingX);
    const int bubblePaddingY = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutPaddingY);
    const int lineGap = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutLineGap);
    const int bubbleRadius = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutRadius);
    const int textLineHeight = std::max(1, dashboardRenderer_.Renderer().TextMetrics().smallText);
    const int gaugeRingThickness =
        std::max(1, dashboardRenderer_.ScaleLogical(dashboardRenderer_.Config().layout.gauge.ringThickness));
    const auto measureCalloutBubble = [&](Callout& callout, std::optional<int> constrainedWidth = std::nullopt) {
        const int parameterWidth =
            std::max(1, dashboardRenderer_.Renderer().MeasureTextWidth(TextStyleId::Small, callout.parameterLine));
        const int descriptionWidth =
            callout.descriptionLine.empty()
                ? 0
                : std::max(
                      1, dashboardRenderer_.Renderer().MeasureTextWidth(TextStyleId::Small, callout.descriptionLine));
        const int contentWidth = std::max(parameterWidth, descriptionWidth);
        const int bubbleWidth = constrainedWidth.value_or(contentWidth + bubblePaddingX * 2);
        const int constrainedContentWidth = std::max(1, bubbleWidth - bubblePaddingX * 2);
        int descriptionHeight = callout.descriptionLine.empty() ? 0 : textLineHeight;
        if (constrainedWidth.has_value() && !callout.descriptionLine.empty()) {
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
        callout.wrapDescription = constrainedWidth.has_value();
    };
    for (Callout& callout : callouts) {
        measureCalloutBubble(callout);
    }

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
        std::optional<std::string> cardId;
        if (callout.hoverAnchorKey.has_value() &&
            callout.hoverAnchorKey->widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            cardId = callout.hoverAnchorKey->widget.renderCardId;
        } else if (callout.hoverWidgetGuide.has_value() &&
                   callout.hoverWidgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            cardId = callout.hoverWidgetGuide->widget.renderCardId;
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
                cardId = cardIt->id;
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
        } else {
            callout.targetRect = TransformRect(callout.targetRect,
                RenderRect{0, 0, dashboardRenderer_.WindowWidth(), dashboardRenderer_.WindowHeight()},
                overview.rect);
        }
    }

    const int cardGap = ScaleNonNegative(dashboardRenderer_, sheetStyle.blockGap);

    for (Callout& callout : callouts) {
        if (callout.sourceCardId == kLayoutGuideSheetOverviewSourceId || !callout.hoverAnchorKey.has_value()) {
            continue;
        }
        const std::optional<LayoutEditAnchorRegion> anchorRegion =
            dashboardRenderer_.FindEditableAnchorRegion(*callout.hoverAnchorKey);
        if (anchorRegion.has_value()) {
            callout.targetRect = anchorRegion->anchorRect;
            callout.hoverAnchorRect = anchorRegion->anchorRect;
            callout.targetAttachmentOnAnchorCircle =
                anchorRegion->shape == AnchorShape::Circle && anchorRegion->dragMode == AnchorDragMode::RadialDistance;
        }
    }

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

    const int rowGap = ScaleNonNegative(dashboardRenderer_, sheetStyle.calloutRowGap);

    const auto targetAttachmentForCallout =
        [&](const Callout& callout, const RenderRect& targetRect, RenderPoint bubbleAttachment) {
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
        };

    struct CardCalloutColumns {
        std::vector<size_t> top;
        std::vector<size_t> left;
        std::vector<size_t> right;
        std::vector<size_t> bottom;
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

    const auto widestBubbleWidthFor = [&](const std::vector<size_t>& plannedIndexes) {
        int width = 0;
        for (const size_t plannedIndex : plannedIndexes) {
            width = std::max(width, callouts[plannedCallouts[plannedIndex].calloutIndex].bubbleRect.Width());
        }
        return width;
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

    const auto computeBlockForColumns = [&](const CardCalloutColumns& columns, const CardPlacement& placement) {
        BlockLayout block;
        block.itemHeight = placement.overview ? overviewHeight : placement.sourceRect.Height();
        block.itemWidth = placement.overview ? overviewWidth : placement.sourceRect.Width();
        block.leftWidth = widestBubbleWidthFor(columns.left);
        block.rightWidth = widestBubbleWidthFor(columns.right);
        const int topWidth = widestBubbleWidthFor(columns.top);
        const int bottomWidth = widestBubbleWidthFor(columns.bottom);
        const int topHeight = stackedHeight(columns.top);
        const int bottomHeight = stackedHeight(columns.bottom);
        const int topProtrusion = topHeight > 0 ? topHeight + calloutGap : 0;
        const int bottomProtrusion = bottomHeight > 0 ? bottomHeight + calloutGap : 0;
        block.itemX = block.leftWidth > 0 ? block.leftWidth + calloutGap : 0;
        const int sideStackHeight = std::max(stackedHeight(columns.left), stackedHeight(columns.right));
        const int sideAbove = std::max(0, (sideStackHeight - block.itemHeight) / 2);
        const int sideBelow = std::max(0, sideStackHeight - block.itemHeight - sideAbove);
        block.itemY = std::max(topProtrusion, sideAbove);
        block.height = block.itemY + block.itemHeight + std::max(bottomProtrusion, sideBelow);
        block.advanceHeight = block.height;
        const int mainWidth =
            block.itemX + block.itemWidth + (block.rightWidth > 0 ? calloutGap + block.rightWidth : 0);
        int topX = block.itemX + (block.itemWidth - topWidth) / 2;
        int bottomX = block.itemX + (block.itemWidth - bottomWidth) / 2;
        const int minX = std::min({0, topX, bottomX});
        const int maxX = std::max({mainWidth, topX + topWidth, bottomX + bottomWidth});
        block.itemX -= minX;
        block.width = maxX - minX;
        return block;
    };

    struct TrialLeader {
        RenderPoint target{};
        RenderPoint bubble{};
    };

    const auto appendTrialLeaders = [&](std::vector<TrialLeader>& leaders,
                                        const std::vector<size_t>& plannedIndexes,
                                        RectExitSide side,
                                        const CardPlacement& placement,
                                        const BlockLayout& block) {
        const RenderRect cardRect{
            block.itemX, block.itemY, block.itemX + block.itemWidth, block.itemY + block.itemHeight};
        int y = cardRect.Center().y - stackedHeight(plannedIndexes) / 2;
        for (const size_t plannedIndex : plannedIndexes) {
            const PlannedCallout& planned = plannedCallouts[plannedIndex];
            const Callout& callout = callouts[planned.calloutIndex];
            const int bubbleX = side == RectExitSide::Left ? block.itemX - calloutGap - callout.bubbleRect.Width()
                                                           : block.itemX + block.itemWidth + calloutGap;
            const RenderRect bubbleRect{
                bubbleX, y, bubbleX + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
            const RenderPoint bubbleAttachment{
                side == RectExitSide::Left ? bubbleRect.right : bubbleRect.left, bubbleRect.Center().y};
            const RenderRect targetRect = placement.overview
                                              ? TransformRect(planned.target, placement.sourceRect, cardRect)
                                              : OffsetRenderRect(planned.target,
                                                    cardRect.left - placement.sourceRect.left,
                                                    cardRect.top - placement.sourceRect.top);
            leaders.push_back(
                TrialLeader{targetAttachmentForCallout(callout, targetRect, bubbleAttachment), bubbleAttachment});
            y = bubbleRect.bottom + rowGap;
        }
    };

    const auto appendTopBottomTrialLeaders = [&](std::vector<TrialLeader>& leaders,
                                                 const std::vector<size_t>& plannedIndexes,
                                                 RectExitSide side,
                                                 const CardPlacement& placement,
                                                 const BlockLayout& block) {
        const RenderRect cardRect{
            block.itemX, block.itemY, block.itemX + block.itemWidth, block.itemY + block.itemHeight};
        for (const size_t plannedIndex : plannedIndexes) {
            const PlannedCallout& planned = plannedCallouts[plannedIndex];
            const Callout& callout = callouts[planned.calloutIndex];
            const int bubbleX = block.itemX + (block.itemWidth - callout.bubbleRect.Width()) / 2;
            const int bubbleY = side == RectExitSide::Top ? block.itemY - calloutGap - callout.bubbleRect.Height()
                                                          : block.itemY + block.itemHeight + calloutGap;
            const RenderRect bubbleRect{
                bubbleX, bubbleY, bubbleX + callout.bubbleRect.Width(), bubbleY + callout.bubbleRect.Height()};
            const RenderPoint bubbleAttachment{
                bubbleRect.Center().x, side == RectExitSide::Top ? bubbleRect.bottom : bubbleRect.top};
            const RenderRect targetRect = placement.overview
                                              ? TransformRect(planned.target, placement.sourceRect, cardRect)
                                              : OffsetRenderRect(planned.target,
                                                    cardRect.left - placement.sourceRect.left,
                                                    cardRect.top - placement.sourceRect.top);
            leaders.push_back(
                TrialLeader{targetAttachmentForCallout(callout, targetRect, bubbleAttachment), bubbleAttachment});
        }
    };

    const auto countLeaderIntersections = [&](const CardCalloutColumns& columns, const CardPlacement& placement) {
        const BlockLayout block = computeBlockForColumns(columns, placement);
        std::vector<TrialLeader> leaders;
        leaders.reserve(columns.top.size() + columns.left.size() + columns.right.size() + columns.bottom.size());
        appendTopBottomTrialLeaders(leaders, columns.top, RectExitSide::Top, placement, block);
        appendTrialLeaders(leaders, columns.left, RectExitSide::Left, placement, block);
        appendTrialLeaders(leaders, columns.right, RectExitSide::Right, placement, block);
        appendTopBottomTrialLeaders(leaders, columns.bottom, RectExitSide::Bottom, placement, block);

        int intersections = 0;
        for (size_t i = 0; i < leaders.size(); ++i) {
            for (size_t j = i + 1; j < leaders.size(); ++j) {
                if (LeaderSegmentsIntersect(
                        leaders[i].target, leaders[i].bubble, leaders[j].target, leaders[j].bubble)) {
                    ++intersections;
                }
            }
        }
        return intersections;
    };

    const auto erasePlannedIndex = [](std::vector<size_t>& indexes, size_t index) {
        indexes.erase(std::remove(indexes.begin(), indexes.end(), index), indexes.end());
    };

    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        const CardCalloutColumns baseColumns = plannedByCard[cardIndex];
        if (baseColumns.left.empty() && baseColumns.right.empty()) {
            continue;
        }
        const size_t noPromotion = (std::numeric_limits<size_t>::max)();
        std::vector<size_t> bottomCandidates = baseColumns.left;
        std::vector<size_t> topCandidates = baseColumns.right;
        if (bottomCandidates.empty()) {
            bottomCandidates.push_back(noPromotion);
        }
        if (topCandidates.empty()) {
            topCandidates.push_back(noPromotion);
        }

        const size_t defaultBottom = baseColumns.left.empty() ? noPromotion : baseColumns.left.back();
        const size_t defaultTop = baseColumns.right.empty() ? noPromotion : baseColumns.right.front();
        int bestIntersections = (std::numeric_limits<int>::max)();
        int bestTieBreak = (std::numeric_limits<int>::max)();
        CardCalloutColumns bestColumns = baseColumns;
        for (const size_t bottomCandidate : bottomCandidates) {
            for (const size_t topCandidate : topCandidates) {
                CardCalloutColumns trial = baseColumns;
                if (bottomCandidate != noPromotion) {
                    erasePlannedIndex(trial.left, bottomCandidate);
                    trial.bottom.push_back(bottomCandidate);
                }
                if (topCandidate != noPromotion) {
                    erasePlannedIndex(trial.right, topCandidate);
                    trial.top.push_back(topCandidate);
                }
                const int intersections = countLeaderIntersections(trial, cardPlacements[cardIndex]);
                const int tieBreak = (bottomCandidate == defaultBottom ? 0 : 1) + (topCandidate == defaultTop ? 0 : 1);
                if (intersections < bestIntersections ||
                    (intersections == bestIntersections && tieBreak < bestTieBreak)) {
                    bestIntersections = intersections;
                    bestTieBreak = tieBreak;
                    bestColumns = std::move(trial);
                }
            }
        }
        plannedByCard[cardIndex] = std::move(bestColumns);
    }

    const auto rectsOverlap = [](const RenderRect& lhs, const RenderRect& rhs) {
        return !lhs.IsEmpty() && !rhs.IsEmpty() && lhs.left < rhs.right && lhs.right > rhs.left &&
               lhs.top < rhs.bottom && lhs.bottom > rhs.top;
    };

    const auto sideStackRect = [&](const std::vector<size_t>& plannedIndexes,
                                   RectExitSide side,
                                   const BlockLayout& block) {
        if (plannedIndexes.empty()) {
            return RenderRect{};
        }
        const int height = stackedHeight(plannedIndexes);
        const int top = block.itemY + block.itemHeight / 2 - height / 2;
        if (side == RectExitSide::Left) {
            return RenderRect{block.itemX - calloutGap - block.leftWidth, top, block.itemX - calloutGap, top + height};
        }
        return RenderRect{block.itemX + block.itemWidth + calloutGap,
            top,
            block.itemX + block.itemWidth + calloutGap + block.rightWidth,
            top + height};
    };

    const auto topBottomBubbleRect = [&](size_t plannedIndex, RectExitSide side, const BlockLayout& block) {
        const Callout& callout = callouts[plannedCallouts[plannedIndex].calloutIndex];
        const int x = block.itemX + (block.itemWidth - callout.bubbleRect.Width()) / 2;
        const int y = side == RectExitSide::Top ? block.itemY - calloutGap - callout.bubbleRect.Height()
                                                : block.itemY + block.itemHeight + calloutGap;
        return RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
    };

    const auto constrainTopBottomIfNeeded = [&](const std::vector<size_t>& plannedIndexes,
                                                RectExitSide side,
                                                const CardCalloutColumns& columns,
                                                const BlockLayout& block) {
        bool changed = false;
        const RenderRect leftStack = sideStackRect(columns.left, RectExitSide::Left, block);
        const RenderRect rightStack = sideStackRect(columns.right, RectExitSide::Right, block);
        for (const size_t plannedIndex : plannedIndexes) {
            Callout& callout = callouts[plannedCallouts[plannedIndex].calloutIndex];
            if (callout.bubbleRect.Width() <= block.itemWidth) {
                continue;
            }
            const RenderRect bubble = topBottomBubbleRect(plannedIndex, side, block);
            if (!rectsOverlap(bubble, leftStack) && !rectsOverlap(bubble, rightStack)) {
                continue;
            }
            measureCalloutBubble(callout, std::max(1, block.itemWidth));
            changed = true;
        }
        return changed;
    };

    for (size_t pass = 0; pass < 3; ++pass) {
        bool changed = false;
        for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
            const BlockLayout block = computeBlockForColumns(plannedByCard[cardIndex], cardPlacements[cardIndex]);
            changed = constrainTopBottomIfNeeded(
                          plannedByCard[cardIndex].top, RectExitSide::Top, plannedByCard[cardIndex], block) ||
                      changed;
            changed = constrainTopBottomIfNeeded(
                          plannedByCard[cardIndex].bottom, RectExitSide::Bottom, plannedByCard[cardIndex], block) ||
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

    const auto placeSide = [&](const std::vector<size_t>& plannedIndexes,
                               RectExitSide side,
                               const RenderRect& cardRect,
                               const BlockLayout& block) {
        int y = cardRect.Center().y - stackedHeight(plannedIndexes) / 2;
        for (const size_t plannedIndex : plannedIndexes) {
            const PlannedCallout& planned = plannedCallouts[plannedIndex];
            Callout& callout = callouts[planned.calloutIndex];
            const int x = side == RectExitSide::Left ? block.itemX - calloutGap - callout.bubbleRect.Width()
                                                     : block.itemX + block.itemWidth + calloutGap;
            callout.bubbleRect = RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
            callout.exitSide = side;
            callout.bubbleAttachment =
                RenderPoint{side == RectExitSide::Left ? callout.bubbleRect.right : callout.bubbleRect.left,
                    callout.bubbleRect.Center().y};
            const int dx = cardRect.left - cardPlacements[planned.cardIndex].sourceRect.left;
            const int dy = cardRect.top - cardPlacements[planned.cardIndex].sourceRect.top;
            const RenderRect targetRect = cardPlacements[planned.cardIndex].overview
                                              ? TransformRect(planned.target,
                                                    cardPlacements[planned.cardIndex].sourceRect,
                                                    cardPlacements[planned.cardIndex].destRect)
                                              : OffsetRenderRect(planned.target, dx, dy);
            callout.targetAttachment = targetAttachmentForCallout(callout, targetRect, callout.bubbleAttachment);
            y = callout.bubbleRect.bottom + rowGap;
        }
    };

    const auto placeTopBottom = [&](const std::vector<size_t>& plannedIndexes,
                                    RectExitSide side,
                                    const RenderRect& cardRect,
                                    const BlockLayout& block) {
        for (const size_t plannedIndex : plannedIndexes) {
            const PlannedCallout& planned = plannedCallouts[plannedIndex];
            Callout& callout = callouts[planned.calloutIndex];
            const int x = block.itemX + (block.itemWidth - callout.bubbleRect.Width()) / 2;
            const int y = side == RectExitSide::Top ? block.itemY - calloutGap - callout.bubbleRect.Height()
                                                    : block.itemY + block.itemHeight + calloutGap;
            callout.bubbleRect = RenderRect{x, y, x + callout.bubbleRect.Width(), y + callout.bubbleRect.Height()};
            callout.exitSide = side;
            callout.bubbleAttachment = RenderPoint{callout.bubbleRect.Center().x,
                side == RectExitSide::Top ? callout.bubbleRect.bottom : callout.bubbleRect.top};
            const int dx = cardRect.left - cardPlacements[planned.cardIndex].sourceRect.left;
            const int dy = cardRect.top - cardPlacements[planned.cardIndex].sourceRect.top;
            const RenderRect targetRect = cardPlacements[planned.cardIndex].overview
                                              ? TransformRect(planned.target,
                                                    cardPlacements[planned.cardIndex].sourceRect,
                                                    cardPlacements[planned.cardIndex].destRect)
                                              : OffsetRenderRect(planned.target, dx, dy);
            callout.targetAttachment = targetAttachmentForCallout(callout, targetRect, callout.bubbleAttachment);
        }
    };

    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        CardPlacement& placement = cardPlacements[cardIndex];
        const BlockLayout& block = blocks[cardIndex];
        placement.destRect =
            RenderRect{block.itemX, block.itemY, block.itemX + block.itemWidth, block.itemY + block.itemHeight};
        placeTopBottom(plannedByCard[cardIndex].top, RectExitSide::Top, placement.destRect, block);
        placeSide(plannedByCard[cardIndex].left, RectExitSide::Left, placement.destRect, block);
        placeSide(plannedByCard[cardIndex].right, RectExitSide::Right, placement.destRect, block);
        placeTopBottom(plannedByCard[cardIndex].bottom, RectExitSide::Bottom, placement.destRect, block);
    }

    const auto validateSideOrder = [&](std::vector<size_t>& plannedIndexes,
                                       RectExitSide side,
                                       const RenderRect& cardRect,
                                       const BlockLayout& block) {
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
                        placeSide(plannedIndexes, side, cardRect, block);
                        swapped = true;
                    }
                }
            }
        }
    };
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        validateSideOrder(
            plannedByCard[cardIndex].left, RectExitSide::Left, cardPlacements[cardIndex].destRect, blocks[cardIndex]);
        validateSideOrder(
            plannedByCard[cardIndex].right, RectExitSide::Right, cardPlacements[cardIndex].destRect, blocks[cardIndex]);
        placeSide(
            plannedByCard[cardIndex].left, RectExitSide::Left, cardPlacements[cardIndex].destRect, blocks[cardIndex]);
        placeSide(
            plannedByCard[cardIndex].right, RectExitSide::Right, cardPlacements[cardIndex].destRect, blocks[cardIndex]);
        placeTopBottom(
            plannedByCard[cardIndex].top, RectExitSide::Top, cardPlacements[cardIndex].destRect, blocks[cardIndex]);
        placeTopBottom(plannedByCard[cardIndex].bottom,
            RectExitSide::Bottom,
            cardPlacements[cardIndex].destRect,
            blocks[cardIndex]);
    }

    const int sheetWidth = sheetMargin * 2 + contentWidth;
    int blockCursorY = sheetMargin;
    int contentBottom = sheetMargin;
    for (size_t cardIndex = 0; cardIndex < cardPlacements.size(); ++cardIndex) {
        const int dx = sheetMargin + (contentWidth - blocks[cardIndex].width) / 2;
        const int dy = blockCursorY;
        CardPlacement& placement = cardPlacements[cardIndex];
        placement.destRect = OffsetRenderRect(placement.destRect, dx, dy);
        contentBottom = std::max(contentBottom, placement.destRect.bottom);
        const auto offsetCallouts = [&](const std::vector<size_t>& plannedIndexes) {
            for (size_t plannedIndex : plannedIndexes) {
                Callout& callout = callouts[plannedCallouts[plannedIndex].calloutIndex];
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
        blockCursorY += blocks[cardIndex].advanceHeight + cardGap;
    }
    const int sheetHeight = cardPlacements.empty() ? sheetMargin * 2 : contentBottom + sheetMargin;

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
        for (const CardPlacement& placement : cardPlacements) {
            if (placement.overview) {
                for (const Callout& callout : callouts) {
                    if (callout.sourceCardId != placement.id) {
                        continue;
                    }
                    if (callout.hoverColorParameter.has_value()) {
                        continue;
                    }
                    DrawOverviewArtifact(dashboardRenderer_,
                        OverviewArtifact{callout.hoverArtifactTargetRect.value_or(callout.targetRect),
                            callout.hoverAnchorRect,
                            callout.hoverGapAnchor,
                            callout.hoverAnchorDrawTargetOutline,
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
            DashboardOverlayState cardBaseOverlayState = overlayState;
            cardBaseOverlayState.hoverOnExposedDashboard = false;
            cardBaseOverlayState.drawExposedDashboardChrome = false;
            cardBaseOverlayState.suppressLayoutGuideContainerHighlights = true;
            dashboardRenderer_.DrawLayoutGuideSheetOverlay(
                cardBaseOverlayState, placement.sourceRect, placement.destRect, metrics);
            for (const Callout& callout : callouts) {
                if (callout.sourceCardId != placement.id) {
                    continue;
                }
                DashboardOverlayState calloutOverlayState = overlayState;
                calloutOverlayState.hoverOnExposedDashboard = false;
                calloutOverlayState.drawExposedDashboardChrome = false;
                calloutOverlayState.suppressLayoutGuideContainerHighlights = true;
                calloutOverlayState.hoveredEditableAnchor = callout.hoverAnchorKey;
                if (callout.hoverAnchorKey.has_value() &&
                    callout.hoverAnchorKey->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
                    calloutOverlayState.hoveredEditableWidget = callout.hoverAnchorKey->widget;
                }
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
