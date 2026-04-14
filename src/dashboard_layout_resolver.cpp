#include "dashboard_layout_resolver.h"

#include "dashboard_renderer.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace {

bool ContainsCardReference(const std::vector<std::string>& stack, const std::string& cardId) {
    return std::find(stack.begin(), stack.end(), cardId) != stack.end();
}

std::string FormatRect(const RenderRect& rect) {
    return "rect=(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," + std::to_string(rect.right) +
           "," + std::to_string(rect.bottom) + ")";
}

RenderRect MakeSquareAnchorRect(int centerX, int centerY, int size) {
    const int clampedSize = (std::max)(4, size);
    const int radius = clampedSize / 2;
    return RenderRect{centerX - radius, centerY - radius, centerX - radius + clampedSize, centerY - radius + clampedSize};
}

RenderRect MakeCircleAnchorRect(int centerX, int centerY, int representedDiameter, int extraDiameter) {
    const int diameter = (std::max)(4, representedDiameter + extraDiameter);
    const int radius = diameter / 2;
    return RenderRect{centerX - radius, centerY - radius, centerX - radius + diameter, centerY - radius + diameter};
}

}  // namespace

void DashboardLayoutResolver::ResolveNodeWidgets(DashboardRenderer& renderer,
    const LayoutNodeConfig& node,
    const RenderRect& rect,
    std::vector<DashboardWidgetLayout>& widgets,
    bool instantiateWidgets) {
    std::vector<std::string> cardReferenceStack;
    ResolveNodeWidgetsInternal(renderer, node, rect, widgets, cardReferenceStack, "", "", {}, instantiateWidgets);
}

void DashboardLayoutResolver::BuildWidgetEditGuides(DashboardRenderer& renderer) {
    renderer.widgetEditGuides_.clear();
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    for (const auto& card : renderer.resolvedLayout_.cards) {
        const DashboardRenderer::LayoutWidgetIdentity cardIdentity{
            card.id, card.id, {}, DashboardRenderer::LayoutWidgetIdentity::Kind::CardChrome};
        auto addCardGuide = [&](DashboardRenderer::LayoutGuideAxis axis,
                                int guideId,
                                DashboardRenderer::LayoutEditParameter parameter,
                                int value,
                                int dragDirection,
                                RenderPoint start,
                                RenderPoint end) {
            DashboardRenderer::WidgetEditGuide guide;
            guide.axis = axis;
            guide.widget = cardIdentity;
            guide.parameter = parameter;
            guide.guideId = guideId;
            guide.widgetRect = card.rect;
            guide.drawStart = start;
            guide.drawEnd = end;
            if (axis == DashboardRenderer::LayoutGuideAxis::Vertical) {
                guide.hitRect = RenderRect{start.x - hitInset, (std::min)(start.y, end.y), start.x + hitInset + 1,
                    (std::max)(start.y, end.y)};
            } else {
                guide.hitRect = RenderRect{(std::min)(start.x, end.x), start.y - hitInset, (std::max)(start.x, end.x),
                    start.y + hitInset + 1};
            }
            guide.value = value;
            guide.dragDirection = dragDirection;
            renderer.widgetEditGuides_.push_back(std::move(guide));
        };

        const int contentLeft = std::clamp(card.contentRect.left, static_cast<int>(card.rect.left), static_cast<int>(card.rect.right));
        const int contentRight =
            std::clamp(card.contentRect.right, contentLeft, static_cast<int>(card.rect.right));
        const int paddingY = std::clamp(card.rect.top + renderer.ScaleLogical(renderer.Config().layout.cardStyle.cardPadding),
            static_cast<int>(card.rect.top),
            static_cast<int>(card.rect.bottom));
        addCardGuide(DashboardRenderer::LayoutGuideAxis::Horizontal,
            0,
            DashboardRenderer::LayoutEditParameter::CardPadding,
            renderer.Config().layout.cardStyle.cardPadding,
            1,
            RenderPoint{contentLeft, paddingY},
            RenderPoint{contentRight, paddingY});

        if (!card.iconName.empty() && !card.title.empty()) {
            const int guideX =
                std::clamp(card.titleRect.left, static_cast<int>(card.rect.left), static_cast<int>(card.rect.right));
            addCardGuide(DashboardRenderer::LayoutGuideAxis::Vertical,
                1,
                DashboardRenderer::LayoutEditParameter::CardHeaderIconGap,
                renderer.Config().layout.cardStyle.headerIconGap,
                1,
                RenderPoint{guideX, card.titleRect.top},
                RenderPoint{guideX, card.titleRect.bottom});
        }

        if (card.hasHeader) {
            const int guideY =
                std::clamp(card.contentRect.top, static_cast<int>(card.rect.top), static_cast<int>(card.rect.bottom));
            addCardGuide(DashboardRenderer::LayoutGuideAxis::Horizontal,
                2,
                DashboardRenderer::LayoutEditParameter::CardHeaderContentGap,
                renderer.Config().layout.cardStyle.headerContentGap,
                1,
                RenderPoint{contentLeft, guideY},
                RenderPoint{contentRight, guideY});
        }

        for (const auto& widget : card.widgets) {
            if (widget.widget != nullptr) {
                widget.widget->BuildEditGuides(renderer, widget);
            }
        }
    }
}

void DashboardLayoutResolver::BuildStaticEditableAnchors(DashboardRenderer& renderer) {
    renderer.staticEditableAnchorRegions_.clear();
    for (const auto& card : renderer.resolvedLayout_.cards) {
        const DashboardRenderer::LayoutWidgetIdentity cardIdentity{
            card.id, card.id, {}, DashboardRenderer::LayoutWidgetIdentity::Kind::CardChrome};
        const int squareAnchorSize = (std::max)(4, renderer.ScaleLogical(6));
        const int radiusLogical = renderer.Config().layout.cardStyle.cardRadius;
        const int radiusScaled = renderer.ScaleLogical(radiusLogical);
        renderer.RegisterStaticEditableAnchorRegion(
            DashboardRenderer::EditableAnchorKey{cardIdentity, DashboardRenderer::LayoutEditParameter::CardRadius, 0},
            {},
            MakeSquareAnchorRect(card.rect.left + radiusScaled, card.rect.top, squareAnchorSize),
            DashboardRenderer::AnchorShape::Square,
            DashboardRenderer::AnchorDragAxis::Vertical,
            DashboardRenderer::AnchorDragMode::AxisDelta,
            RenderPoint{card.rect.left + radiusScaled, card.rect.top},
            1.0,
            true,
            false,
            radiusLogical);

        const int borderScaled = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.cardStyle.cardBorderWidth));
        const int borderAnchorPadding = (std::max)(1, renderer.ScaleLogical(1));
        const int borderCenterX = card.rect.left + (std::max)(0, (card.rect.right - card.rect.left) / 2);
        const int borderCenterY = card.rect.top;
        renderer.RegisterStaticEditableAnchorRegion(
            DashboardRenderer::EditableAnchorKey{cardIdentity, DashboardRenderer::LayoutEditParameter::CardBorder, 0},
            {},
            MakeCircleAnchorRect(borderCenterX, borderCenterY, borderScaled, borderAnchorPadding),
            DashboardRenderer::AnchorShape::Circle,
            DashboardRenderer::AnchorDragAxis::Both,
            DashboardRenderer::AnchorDragMode::RadialDistance,
            RenderPoint{borderCenterX, borderCenterY},
            2.0,
            true,
            false,
            renderer.Config().layout.cardStyle.cardBorderWidth);

        if (!card.iconName.empty()) {
            const int anchorCenterX = card.iconRect.right;
            const int anchorCenterY = card.iconRect.top;
            renderer.RegisterStaticEditableAnchorRegion(
                DashboardRenderer::EditableAnchorKey{
                    cardIdentity, DashboardRenderer::LayoutEditParameter::CardHeaderIconSize, 0},
                card.iconRect,
                MakeSquareAnchorRect(anchorCenterX, anchorCenterY, squareAnchorSize),
                DashboardRenderer::AnchorShape::Square,
                DashboardRenderer::AnchorDragAxis::Vertical,
                DashboardRenderer::AnchorDragMode::AxisDelta,
                RenderPoint{anchorCenterX, anchorCenterY},
                1.0,
                false,
                true,
                renderer.Config().layout.cardStyle.headerIconSize);
        }

        if (!card.title.empty()) {
            renderer.RegisterStaticTextAnchor(card.titleRect,
                card.title,
                TextStyleId::Title,
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
                DashboardRenderer::EditableAnchorBinding{
                    DashboardRenderer::EditableAnchorKey{
                        DashboardRenderer::LayoutWidgetIdentity{card.id, card.id, {}},
                        DashboardRenderer::LayoutEditParameter::FontTitle,
                        0,
                    },
                    renderer.Config().layout.fonts.title.size,
                    DashboardRenderer::AnchorShape::Circle,
                    DashboardRenderer::AnchorDragAxis::Vertical,
                    DashboardRenderer::AnchorDragMode::AxisDelta,
                });
        }
        for (const auto& widget : card.widgets) {
            if (widget.widget != nullptr) {
                widget.widget->BuildStaticAnchors(renderer, widget);
            }
        }
    }
}

void DashboardLayoutResolver::AddLayoutEditGuide(DashboardRenderer& renderer,
    const LayoutNodeConfig& node,
    const RenderRect& rect,
    const std::vector<RenderRect>& childRects,
    int gap,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath) {
    if (!DashboardRenderer::IsContainerNode(node) || childRects.size() < 2) {
        return;
    }

    const bool horizontal = node.name == "columns";
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    std::vector<bool> childFixedExtents;
    childFixedExtents.reserve(node.children.size());
    for (const auto& child : node.children) {
        const DashboardRenderer::ParsedWidgetInfo* childWidget = renderer.FindParsedWidgetInfo(child);
        childFixedExtents.push_back(!horizontal && childWidget != nullptr &&
                                    (childWidget->fixedPreferredHeightInRows || childWidget->verticalSpring));
    }
    for (size_t i = 0; i + 1 < childRects.size(); ++i) {
        if (!horizontal && (childFixedExtents[i] || childFixedExtents[i + 1])) {
            continue;
        }
        DashboardRenderer::LayoutEditGuide guide;
        guide.axis =
            horizontal ? DashboardRenderer::LayoutGuideAxis::Vertical : DashboardRenderer::LayoutGuideAxis::Horizontal;
        guide.renderCardId = renderCardId;
        guide.editCardId = editCardId;
        guide.nodePath = nodePath;
        guide.separatorIndex = i;
        guide.containerRect = rect;
        guide.gap = gap;
        guide.childExtents.reserve(childRects.size());
        guide.childFixedExtents = childFixedExtents;
        guide.childRects = childRects;
        for (const RenderRect& childRect : childRects) {
            guide.childExtents.push_back(
                horizontal ? (childRect.right - childRect.left) : (childRect.bottom - childRect.top));
        }

        if (horizontal) {
            const int x = childRects[i].right + (std::max)(0, gap / 2);
            guide.lineRect = RenderRect{x, rect.top, x + 1, rect.bottom};
            guide.hitRect = RenderRect{x - hitInset, rect.top, x + hitInset + 1, rect.bottom};
        } else {
            const int y = childRects[i].bottom + (std::max)(0, gap / 2);
            guide.lineRect = RenderRect{rect.left, y, rect.right, y + 1};
            guide.hitRect = RenderRect{rect.left, y - hitInset, rect.right, y + hitInset + 1};
        }
        renderer.layoutEditGuides_.push_back(std::move(guide));
    }
}

void DashboardLayoutResolver::ResolveNodeWidgetsInternal(DashboardRenderer& renderer,
    const LayoutNodeConfig& node,
    const RenderRect& rect,
    std::vector<DashboardWidgetLayout>& widgets,
    std::vector<std::string>& cardReferenceStack,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath,
    bool instantiateWidgets) {
    renderer.WriteTrace("renderer:layout_resolve_node name=\"" + node.name +
                        "\" weight=" + std::to_string(node.weight) + " " + FormatRect(rect) +
                        " children=" + std::to_string(node.children.size()));
    if (node.cardReference) {
        if (ContainsCardReference(cardReferenceStack, node.name)) {
            renderer.WriteTrace("renderer:layout_card_ref_cycle id=\"" + node.name + "\"");
            return;
        }
        const LayoutCardConfig* referencedCard = renderer.FindCardConfigById(node.name);
        if (referencedCard == nullptr) {
            renderer.WriteTrace("renderer:layout_card_ref_missing id=\"" + node.name + "\"");
            return;
        }
        renderer.WriteTrace("renderer:layout_card_ref id=\"" + node.name + "\" " + FormatRect(rect));
        cardReferenceStack.push_back(node.name);
        ResolveNodeWidgetsInternal(renderer,
            referencedCard->layout,
            rect,
            widgets,
            cardReferenceStack,
            renderCardId,
            node.name,
            {},
            instantiateWidgets);
        cardReferenceStack.pop_back();
        return;
    }
    if (!DashboardRenderer::IsContainerNode(node)) {
        DashboardWidgetLayout widget = renderer.ResolveWidgetLayout(node, rect, instantiateWidgets);
        widget.cardId = renderCardId;
        widget.editCardId = editCardId;
        widget.nodePath = nodePath;
        const std::string widgetTypeName =
            widget.widget != nullptr ? std::string(DashboardWidgetClassName(widget.widget->Class())) : std::string();
        renderer.WriteTrace("renderer:layout_widget_resolved kind=\"" + node.name + "\" " + FormatRect(widget.rect) +
                            (widgetTypeName.empty() ? "" : " type=\"" + widgetTypeName + "\""));
        widgets.push_back(std::move(widget));
        return;
    }

    const bool horizontal = node.name == "columns";
    const int gap = horizontal ? renderer.ScaleLogical(renderer.config_.layout.cardStyle.columnGap)
                               : renderer.ScaleLogical(renderer.config_.layout.cardStyle.rowGap);

    const int totalAvailable = (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
                               gap * static_cast<int>((std::max)(static_cast<size_t>(0), node.children.size() - 1));
    int reservedPreferred = 0;
    int totalWeight = 0;
    int springWeight = 0;
    const bool rowsUseSprings =
        !horizontal && std::any_of(node.children.begin(), node.children.end(), [&](const auto& child) {
            const DashboardRenderer::ParsedWidgetInfo* childWidget = renderer.FindParsedWidgetInfo(child);
            return childWidget != nullptr && childWidget->verticalSpring;
        });
    if (!horizontal) {
        for (const auto& child : node.children) {
            const DashboardRenderer::ParsedWidgetInfo* childWidget = renderer.FindParsedWidgetInfo(child);
            if (childWidget != nullptr && childWidget->verticalSpring) {
                springWeight += (std::max)(1, child.weight);
                continue;
            }
            if (childWidget != nullptr && childWidget->fixedPreferredHeightInRows) {
                reservedPreferred += (std::max)(0, childWidget->preferredHeight);
            } else if (rowsUseSprings) {
                reservedPreferred += (std::max)(0, renderer.PreferredNodeHeight(child, rect.right - rect.left));
            } else {
                totalWeight += (std::max)(1, child.weight);
            }
        }
    } else {
        for (const auto& child : node.children) {
            totalWeight += (std::max)(1, child.weight);
        }
    }
    const int distributableAvailable = horizontal ? totalAvailable : (std::max)(0, totalAvailable - reservedPreferred);
    if (horizontal && totalWeight <= 0) {
        return;
    }

    int remainingDistributable = distributableAvailable;
    int cursor = horizontal ? rect.left : rect.top;
    std::vector<RenderRect> childRects;
    childRects.reserve(node.children.size());
    for (size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = node.children[i];
        const DashboardRenderer::ParsedWidgetInfo* childWidget = renderer.FindParsedWidgetInfo(child);
        const bool fixedPreferred = !horizontal && childWidget != nullptr && childWidget->fixedPreferredHeightInRows;
        const bool verticalSpring = !horizontal && childWidget != nullptr && childWidget->verticalSpring;
        const bool preferredPacked = !horizontal && rowsUseSprings && !verticalSpring;
        const int childWeight = (fixedPreferred || preferredPacked) ? 0 : (std::max)(1, child.weight);
        const int remainingWeight = (std::max)(1, totalWeight);
        int size = 0;
        if (fixedPreferred) {
            size = (std::max)(0, childWidget->preferredHeight);
        } else if (preferredPacked) {
            size = (std::max)(0, renderer.PreferredNodeHeight(child, rect.right - rect.left));
        } else if (verticalSpring) {
            if (i + 1 == node.children.size()) {
                size = remainingDistributable;
            } else {
                size =
                    (std::max)(0, remainingDistributable * (std::max)(1, child.weight) / (std::max)(1, springWeight));
            }
        } else if (i + 1 == node.children.size()) {
            size = (horizontal ? rect.right : rect.bottom) - cursor;
        } else {
            size = (std::max)(0, remainingDistributable * childWeight / remainingWeight);
        }
        const int remainingExtent = (std::max)(0, (horizontal ? rect.right : rect.bottom) - cursor);
        size = (std::min)((std::max)(0, size), remainingExtent);

        RenderRect childRect = rect;
        if (horizontal) {
            childRect.left = cursor;
            childRect.right = cursor + size;
        } else {
            childRect.top = cursor;
            childRect.bottom = cursor + size;
        }

        renderer.WriteTrace("renderer:layout_weighted_child parent=\"" + node.name + "\" child=\"" + child.name +
                            "\" weight=" + std::to_string(childWeight) + " gap=" + std::to_string(gap) +
                            " size=" + std::to_string(size) + " " + FormatRect(childRect));
        childRects.push_back(childRect);
        std::vector<size_t> childPath = nodePath;
        childPath.push_back(i);
        ResolveNodeWidgetsInternal(renderer,
            child,
            childRect,
            widgets,
            cardReferenceStack,
            renderCardId,
            editCardId,
            childPath,
            instantiateWidgets);
        cursor += size + gap;
        if (verticalSpring) {
            remainingDistributable -= size;
            springWeight -= (std::max)(1, child.weight);
        } else if (!fixedPreferred && !preferredPacked) {
            remainingDistributable -= size;
            totalWeight -= childWeight;
        }
    }
    if (instantiateWidgets) {
        AddLayoutEditGuide(renderer, node, rect, childRects, gap, renderCardId, editCardId, nodePath);
    }
}

bool DashboardLayoutResolver::ResolveLayout(DashboardRenderer& renderer, bool includeWidgetState) {
    renderer.resolvedLayout_ = {};
    renderer.layoutEditGuides_.clear();
    renderer.widgetEditGuides_.clear();
    renderer.staticEditableAnchorRegions_.clear();
    renderer.dynamicEditableAnchorRegions_.clear();
    renderer.parsedWidgetInfoCache_.clear();
    renderer.resolvedLayout_.windowWidth = renderer.WindowWidth();
    renderer.resolvedLayout_.windowHeight = renderer.WindowHeight();

    const RenderRect dashboardRect{renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
        renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
        renderer.WindowWidth() - renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
        renderer.WindowHeight() - renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin)};

    if (renderer.config_.layout.structure.cardsLayout.name.empty()) {
        renderer.lastError_ = "renderer:layout_missing_cards_root";
        return false;
    }

    renderer.WriteTrace("renderer:layout_begin window=" + std::to_string(renderer.resolvedLayout_.windowWidth) + "x" +
                        std::to_string(renderer.resolvedLayout_.windowHeight) + " " + FormatRect(dashboardRect) +
                        " cards_root=\"" + renderer.config_.layout.structure.cardsLayout.name + "\"");

    const auto resolveCard = [&](const LayoutNodeConfig& node, const RenderRect& rect) {
        const auto cardIt = std::find_if(renderer.config_.layout.cards.begin(),
            renderer.config_.layout.cards.end(),
            [&](const auto& card) { return card.id == node.name; });
        if (cardIt == renderer.config_.layout.cards.end()) {
            return;
        }

        DashboardRenderer::ResolvedCardLayout card;
        card.id = cardIt->id;
        card.title = cardIt->title;
        card.iconName = cardIt->icon;
        card.hasHeader = !card.title.empty() || !card.iconName.empty();
        card.rect = rect;

        const int padding = renderer.ScaleLogical(renderer.config_.layout.cardStyle.cardPadding);
        const int iconSize = renderer.ScaleLogical(renderer.config_.layout.cardStyle.headerIconSize);
        const int headerHeight = card.hasHeader ? (std::max)(renderer.TextMetrics().title, iconSize) : 0;
        if (!card.iconName.empty()) {
            card.iconRect = RenderRect{card.rect.left + padding,
                card.rect.top + padding + (std::max)(0, (headerHeight - iconSize) / 2),
                card.rect.left + padding + iconSize,
                card.rect.top + padding + (std::max)(0, (headerHeight - iconSize) / 2) + iconSize};
        } else {
            card.iconRect = RenderRect{
                card.rect.left + padding, card.rect.top + padding, card.rect.left + padding, card.rect.top + padding};
        }
        const int titleLeft =
            !card.iconName.empty()
                ? card.iconRect.right + renderer.ScaleLogical(renderer.config_.layout.cardStyle.headerIconGap)
                : card.rect.left + padding;
        card.titleRect = RenderRect{
            titleLeft, card.rect.top + padding, card.rect.right - padding, card.rect.top + padding + headerHeight};
        card.contentRect = RenderRect{card.rect.left + padding,
            card.rect.top + padding + headerHeight +
                (card.hasHeader ? renderer.ScaleLogical(renderer.config_.layout.cardStyle.headerContentGap) : 0),
            card.rect.right - padding,
            card.rect.bottom - padding};

        renderer.WriteTrace("renderer:layout_card id=\"" + card.id + "\" " + FormatRect(card.rect) +
                            " title=" + FormatRect(card.titleRect) + " icon=" + FormatRect(card.iconRect) +
                            " content=" + FormatRect(card.contentRect));
        std::vector<std::string> cardReferenceStack;
        ResolveNodeWidgetsInternal(renderer,
            cardIt->layout,
            card.contentRect,
            card.widgets,
            cardReferenceStack,
            card.id,
            card.id,
            {},
            includeWidgetState);
        renderer.resolvedLayout_.cards.push_back(std::move(card));
    };

    std::function<void(const LayoutNodeConfig&, const RenderRect&, const std::vector<size_t>&)> resolveDashboardNode =
        [&](const LayoutNodeConfig& node, const RenderRect& rect, const std::vector<size_t>& nodePath) {
            if (!DashboardRenderer::IsContainerNode(node)) {
                resolveCard(node, rect);
                return;
            }

            const bool horizontal = node.name == "columns";
            const int gap = horizontal ? renderer.ScaleLogical(renderer.config_.layout.dashboard.cardGap)
                                       : renderer.ScaleLogical(renderer.config_.layout.dashboard.rowGap);
            int totalWeight = 0;
            for (const auto& child : node.children) {
                totalWeight += (std::max)(1, child.weight);
            }
            if (totalWeight <= 0) {
                return;
            }

            const int totalAvailable =
                (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
                gap * static_cast<int>((std::max)(static_cast<size_t>(0), node.children.size() - 1));
            int remainingAvailable = totalAvailable;
            int cursor = horizontal ? rect.left : rect.top;
            int remainingWeight = totalWeight;
            std::vector<RenderRect> childRects;
            childRects.reserve(node.children.size());
            for (size_t i = 0; i < node.children.size(); ++i) {
                const auto& child = node.children[i];
                const int childWeight = (std::max)(1, child.weight);
                const int size = (i + 1 == node.children.size())
                                     ? ((horizontal ? rect.right : rect.bottom) - cursor)
                                     : (std::max)(0, remainingAvailable * childWeight / (std::max)(1, remainingWeight));

                RenderRect childRect = rect;
                if (horizontal) {
                    childRect.left = cursor;
                    childRect.right = cursor + size;
                } else {
                    childRect.top = cursor;
                    childRect.bottom = cursor + size;
                }

                renderer.WriteTrace("renderer:layout_dashboard_child parent=\"" + node.name + "\" child=\"" +
                                    child.name + "\" weight=" + std::to_string(childWeight) +
                                    " gap=" + std::to_string(gap) + " size=" + std::to_string(size) + " " +
                                    FormatRect(childRect));
                childRects.push_back(childRect);
                std::vector<size_t> childPath = nodePath;
                childPath.push_back(i);
                resolveDashboardNode(child, childRect, childPath);
                cursor += size + gap;
                remainingAvailable -= size;
                remainingWeight -= childWeight;
            }
            if (includeWidgetState) {
                AddLayoutEditGuide(renderer, node, rect, childRects, gap, "", "", nodePath);
            }
        };

    resolveDashboardNode(renderer.config_.layout.structure.cardsLayout, dashboardRect, {});

    if (renderer.resolvedLayout_.cards.empty()) {
        renderer.lastError_ = "renderer:layout_resolve_failed cards=0 root=\"" +
                              renderer.config_.layout.structure.cardsLayout.name + "\"";
        return false;
    }

    if (includeWidgetState) {
        std::map<DashboardWidgetClass, std::vector<DashboardWidgetLayout*>> widgetGroups;
        for (auto& card : renderer.resolvedLayout_.cards) {
            for (auto& widget : card.widgets) {
                if (widget.widget == nullptr) {
                    continue;
                }
                widgetGroups[widget.widget->Class()].push_back(&widget);
            }
        }
        for (auto& [_, group] : widgetGroups) {
            if (!group.empty() && group.front()->widget != nullptr) {
                group.front()->widget->FinalizeLayoutGroup(renderer, group);
            }
        }
        for (auto& card : renderer.resolvedLayout_.cards) {
            for (auto& widget : card.widgets) {
                if (widget.widget != nullptr) {
                    widget.widget->ResolveLayoutState(renderer, widget.rect);
                }
            }
        }

        if (renderer.layoutGuideDragActive_) {
            renderer.widgetEditGuides_.clear();
            renderer.staticEditableAnchorRegions_.clear();
        } else {
            BuildWidgetEditGuides(renderer);
            BuildStaticEditableAnchors(renderer);
        }
    }

    renderer.WriteTrace("renderer:layout_done cards=" + std::to_string(renderer.resolvedLayout_.cards.size()));
    return true;
}
