#include "dashboard_renderer_layout_engine.h"

#include "dashboard_renderer.h"

#include <algorithm>
#include <functional>
#include <map>

namespace {

std::string FormatRect(const RECT& rect) {
    return "rect=(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," + std::to_string(rect.right) +
           "," + std::to_string(rect.bottom) + ")";
}

}  // namespace

bool DashboardRendererLayoutEngine::ResolveLayout(DashboardRenderer& renderer) {
    renderer.resolvedLayout_ = {};
    renderer.layoutEditGuides_.clear();
    renderer.widgetEditGuides_.clear();
    renderer.parsedWidgetInfoCache_.clear();
    renderer.resolvedLayout_.windowWidth = renderer.WindowWidth();
    renderer.resolvedLayout_.windowHeight = renderer.WindowHeight();

    const RECT dashboardRect{renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
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

    const auto resolveCard = [&](const LayoutNodeConfig& node, const RECT& rect) {
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
        const int headerHeight = card.hasHeader ? renderer.EffectiveHeaderHeight() : 0;
        if (!card.iconName.empty()) {
            card.iconRect = RECT{card.rect.left + padding,
                card.rect.top + padding + (std::max)(0, (headerHeight - iconSize) / 2),
                card.rect.left + padding + iconSize,
                card.rect.top + padding + (std::max)(0, (headerHeight - iconSize) / 2) + iconSize};
        } else {
            card.iconRect = RECT{
                card.rect.left + padding, card.rect.top + padding, card.rect.left + padding, card.rect.top + padding};
        }
        const int titleLeft =
            !card.iconName.empty()
                ? card.iconRect.right + renderer.ScaleLogical(renderer.config_.layout.cardStyle.headerGap)
                : card.rect.left + padding;
        card.titleRect =
            RECT{titleLeft, card.rect.top + padding, card.rect.right - padding, card.rect.top + padding + headerHeight};
        card.contentRect = RECT{card.rect.left + padding,
            card.rect.top + padding + headerHeight +
                renderer.ScaleLogical(renderer.config_.layout.cardStyle.contentGap),
            card.rect.right - padding,
            card.rect.bottom - padding};

        renderer.WriteTrace("renderer:layout_card id=\"" + card.id + "\" " + FormatRect(card.rect) +
                            " title=" + FormatRect(card.titleRect) + " icon=" + FormatRect(card.iconRect) +
                            " content=" + FormatRect(card.contentRect));
        std::vector<std::string> cardReferenceStack;
        renderer.ResolveNodeWidgetsInternal(
            cardIt->layout, card.contentRect, card.widgets, cardReferenceStack, card.id, card.id, {});
        renderer.resolvedLayout_.cards.push_back(std::move(card));
    };

    std::function<void(const LayoutNodeConfig&, const RECT&, const std::vector<size_t>&)> resolveDashboardNode =
        [&](const LayoutNodeConfig& node, const RECT& rect, const std::vector<size_t>& nodePath) {
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
            std::vector<RECT> childRects;
            childRects.reserve(node.children.size());
            for (size_t i = 0; i < node.children.size(); ++i) {
                const auto& child = node.children[i];
                const int childWeight = (std::max)(1, child.weight);
                const int size = (i + 1 == node.children.size())
                                     ? ((horizontal ? rect.right : rect.bottom) - cursor)
                                     : (std::max)(0, remainingAvailable * childWeight / (std::max)(1, remainingWeight));

                RECT childRect = rect;
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
            renderer.AddLayoutEditGuide(node, rect, childRects, gap, "", "", nodePath);
        };

    resolveDashboardNode(renderer.config_.layout.structure.cardsLayout, dashboardRect, {});

    if (renderer.resolvedLayout_.cards.empty()) {
        renderer.lastError_ = "renderer:layout_resolve_failed cards=0 root=\"" +
                              renderer.config_.layout.structure.cardsLayout.name + "\"";
        return false;
    }

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

    BuildWidgetEditGuides(renderer);

    renderer.WriteTrace("renderer:layout_done cards=" + std::to_string(renderer.resolvedLayout_.cards.size()));
    return true;
}

void DashboardRendererLayoutEngine::BuildWidgetEditGuides(DashboardRenderer& renderer) {
    renderer.widgetEditGuides_.clear();
    for (const auto& card : renderer.resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.widget != nullptr) {
                widget.widget->BuildEditGuides(renderer, widget);
            }
        }
    }
}
