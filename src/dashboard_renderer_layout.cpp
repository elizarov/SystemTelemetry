#include "dashboard_renderer.h"
#include "dashboard_renderer_layout_engine.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace {

bool ContainsCardReference(const std::vector<std::string>& stack, const std::string& cardId) {
    return std::find(stack.begin(), stack.end(), cardId) != stack.end();
}

std::string FormatRect(const RECT& rect) {
    return "rect=(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," + std::to_string(rect.right) +
           "," + std::to_string(rect.bottom) + ")";
}

}  // namespace

void DashboardRenderer::ResolveNodeWidgets(
    const LayoutNodeConfig& node, const RECT& rect, std::vector<DashboardWidgetLayout>& widgets) {
    std::vector<std::string> cardReferenceStack;
    ResolveNodeWidgetsInternal(node, rect, widgets, cardReferenceStack, "", "", {});
}

void DashboardRenderer::BuildWidgetEditGuides() {
    DashboardRendererLayoutEngine::BuildWidgetEditGuides(*this);
}

void DashboardRenderer::AddLayoutEditGuide(const LayoutNodeConfig& node,
    const RECT& rect,
    const std::vector<RECT>& childRects,
    int gap,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath) {
    if (!IsContainerNode(node) || childRects.size() < 2) {
        return;
    }

    const bool horizontal = node.name == "columns";
    const int hitInset = std::max(3, ScaleLogical(4));
    std::vector<bool> childFixedExtents;
    childFixedExtents.reserve(node.children.size());
    for (const auto& child : node.children) {
        const DashboardWidgetLayout resolvedChild = ResolveWidgetLayout(child, RECT{});
        childFixedExtents.push_back(
            !horizontal && (UsesFixedPreferredHeightInRows(resolvedChild) ||
                               (resolvedChild.widget != nullptr && resolvedChild.widget->IsVerticalSpring())));
    }
    for (size_t i = 0; i + 1 < childRects.size(); ++i) {
        if (!horizontal && (childFixedExtents[i] || childFixedExtents[i + 1])) {
            continue;
        }
        LayoutEditGuide guide;
        guide.axis = horizontal ? LayoutGuideAxis::Vertical : LayoutGuideAxis::Horizontal;
        guide.renderCardId = renderCardId;
        guide.editCardId = editCardId;
        guide.nodePath = nodePath;
        guide.separatorIndex = i;
        guide.containerRect = rect;
        guide.gap = gap;
        guide.childExtents.reserve(childRects.size());
        guide.childFixedExtents = childFixedExtents;
        guide.childRects = childRects;
        for (const RECT& childRect : childRects) {
            guide.childExtents.push_back(
                horizontal ? (childRect.right - childRect.left) : (childRect.bottom - childRect.top));
        }

        if (horizontal) {
            const int x = childRects[i].right + std::max(0, gap / 2);
            guide.lineRect = RECT{x, rect.top, x + 1, rect.bottom};
            guide.hitRect = RECT{x - hitInset, rect.top, x + hitInset + 1, rect.bottom};
        } else {
            const int y = childRects[i].bottom + std::max(0, gap / 2);
            guide.lineRect = RECT{rect.left, y, rect.right, y + 1};
            guide.hitRect = RECT{rect.left, y - hitInset, rect.right, y + hitInset + 1};
        }
        layoutEditGuides_.push_back(std::move(guide));
    }
}

void DashboardRenderer::ResolveNodeWidgetsInternal(const LayoutNodeConfig& node,
    const RECT& rect,
    std::vector<DashboardWidgetLayout>& widgets,
    std::vector<std::string>& cardReferenceStack,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath) {
    WriteTrace("renderer:layout_resolve_node name=\"" + node.name + "\" weight=" + std::to_string(node.weight) + " " +
               FormatRect(rect) + " children=" + std::to_string(node.children.size()));
    if (node.cardReference) {
        if (ContainsCardReference(cardReferenceStack, node.name)) {
            WriteTrace("renderer:layout_card_ref_cycle id=\"" + node.name + "\"");
            return;
        }
        const LayoutCardConfig* referencedCard = FindCardConfigById(node.name);
        if (referencedCard == nullptr) {
            WriteTrace("renderer:layout_card_ref_missing id=\"" + node.name + "\"");
            return;
        }
        WriteTrace("renderer:layout_card_ref id=\"" + node.name + "\" " + FormatRect(rect));
        cardReferenceStack.push_back(node.name);
        ResolveNodeWidgetsInternal(
            referencedCard->layout, rect, widgets, cardReferenceStack, renderCardId, node.name, {});
        cardReferenceStack.pop_back();
        return;
    }
    if (!IsContainerNode(node)) {
        DashboardWidgetLayout widget = ResolveWidgetLayout(node, rect);
        widget.cardId = renderCardId;
        widget.editCardId = editCardId;
        widget.nodePath = nodePath;
        WriteTrace("renderer:layout_widget_resolved kind=\"" + node.name + "\" " + FormatRect(widget.rect) +
                   (widget.typeName.empty() ? "" : " type=\"" + widget.typeName + "\""));
        widgets.push_back(std::move(widget));
        return;
    }

    const bool horizontal = node.name == "columns";
    const int gap = horizontal ? ScaleLogical(config_.layout.cardStyle.columnGap)
                               : ScaleLogical(config_.layout.cardStyle.widgetLineGap);

    const int totalAvailable = (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
                               gap * static_cast<int>(std::max<size_t>(0, node.children.size() - 1));
    int reservedPreferred = 0;
    int totalWeight = 0;
    int springWeight = 0;
    const bool rowsUseSprings =
        !horizontal && std::any_of(node.children.begin(), node.children.end(), [&](const auto& child) {
            const DashboardWidgetLayout resolvedChild = ResolveWidgetLayout(child, RECT{});
            return resolvedChild.widget != nullptr && resolvedChild.widget->IsVerticalSpring();
        });
    if (!horizontal) {
        for (const auto& child : node.children) {
            const DashboardWidgetLayout resolvedChild = ResolveWidgetLayout(child, RECT{});
            if (resolvedChild.widget != nullptr && resolvedChild.widget->IsVerticalSpring()) {
                springWeight += std::max(1, child.weight);
                continue;
            }
            if (UsesFixedPreferredHeightInRows(resolvedChild)) {
                reservedPreferred += std::max(0, resolvedChild.preferredHeight);
            } else if (rowsUseSprings) {
                reservedPreferred += std::max(0, PreferredNodeHeight(child, static_cast<int>(rect.right - rect.left)));
            } else {
                totalWeight += std::max(1, child.weight);
            }
        }
    } else {
        for (const auto& child : node.children) {
            totalWeight += std::max(1, child.weight);
        }
    }
    const int distributableAvailable = horizontal ? totalAvailable : std::max(0, totalAvailable - reservedPreferred);
    if (horizontal && totalWeight <= 0) {
        return;
    }

    int remainingAvailable = totalAvailable;
    int remainingDistributable = distributableAvailable;
    int cursor = horizontal ? rect.left : rect.top;
    std::vector<RECT> childRects;
    childRects.reserve(node.children.size());
    for (size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = node.children[i];
        const DashboardWidgetLayout resolvedChild = ResolveWidgetLayout(child, RECT{});
        const bool fixedPreferred = !horizontal && UsesFixedPreferredHeightInRows(resolvedChild);
        const bool verticalSpring =
            !horizontal && resolvedChild.widget != nullptr && resolvedChild.widget->IsVerticalSpring();
        const bool preferredPacked = !horizontal && rowsUseSprings && !verticalSpring;
        const int childWeight = (fixedPreferred || preferredPacked) ? 0 : std::max(1, child.weight);
        const int remainingWeight = std::max(1, totalWeight);
        int size = 0;
        if (fixedPreferred) {
            size = std::max(0, resolvedChild.preferredHeight);
        } else if (preferredPacked) {
            size = std::max(0, PreferredNodeHeight(child, static_cast<int>(rect.right - rect.left)));
        } else if (verticalSpring) {
            if (i + 1 == node.children.size()) {
                size = remainingDistributable;
            } else {
                size = std::max(0, remainingDistributable * std::max(1, child.weight) / std::max(1, springWeight));
            }
        } else if (i + 1 == node.children.size()) {
            size = (horizontal ? rect.right : rect.bottom) - cursor;
        } else {
            size = std::max(0, remainingDistributable * childWeight / remainingWeight);
        }
        const int remainingExtent = std::max(0, static_cast<int>((horizontal ? rect.right : rect.bottom) - cursor));
        size = std::min(std::max(0, size), remainingExtent);

        RECT childRect = rect;
        if (horizontal) {
            childRect.left = cursor;
            childRect.right = cursor + size;
        } else {
            childRect.top = cursor;
            childRect.bottom = cursor + size;
        }

        WriteTrace("renderer:layout_weighted_child parent=\"" + node.name + "\" child=\"" + child.name +
                   "\" weight=" + std::to_string(childWeight) + " gap=" + std::to_string(gap) +
                   " size=" + std::to_string(size) + " " + FormatRect(childRect));
        childRects.push_back(childRect);
        std::vector<size_t> childPath = nodePath;
        childPath.push_back(i);
        ResolveNodeWidgetsInternal(child, childRect, widgets, cardReferenceStack, renderCardId, editCardId, childPath);
        cursor += size + gap;
        remainingAvailable -= size;
        if (verticalSpring) {
            remainingDistributable -= size;
            springWeight -= std::max(1, child.weight);
        } else if (!fixedPreferred && !preferredPacked) {
            remainingDistributable -= size;
            totalWeight -= childWeight;
        }
    }
    AddLayoutEditGuide(node, rect, childRects, gap, renderCardId, editCardId, nodePath);
}

bool DashboardRenderer::ResolveLayout() {
    return DashboardRendererLayoutEngine::ResolveLayout(*this);
}
