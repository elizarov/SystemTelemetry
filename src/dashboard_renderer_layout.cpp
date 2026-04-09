#include "dashboard_renderer.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace {

bool ContainsCardReference(const std::vector<std::string>& stack, const std::string& cardId) {
    return std::find(stack.begin(), stack.end(), cardId) != stack.end();
}

std::string FormatRect(const RECT& rect) {
    return "rect=(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," +
        std::to_string(rect.right) + "," + std::to_string(rect.bottom) + ")";
}

}  // namespace

void DashboardRenderer::ResolveNodeWidgets(const LayoutNodeConfig& node, const RECT& rect, std::vector<ResolvedWidgetLayout>& widgets) {
    std::vector<std::string> cardReferenceStack;
    ResolveNodeWidgetsInternal(node, rect, widgets, cardReferenceStack, "", "", {});
}

void DashboardRenderer::AddMetricListWidgetEditGuides(const ResolvedWidgetLayout& widget) {
    const int labelWidth = std::max(1, ScaleLogical(config_.layout.metricList.labelWidth));
    const int rowHeight = EffectiveMetricRowHeight();
    const int hitInset = std::max(3, ScaleLogical(4));
    const int x = std::clamp(static_cast<int>(widget.rect.left) + labelWidth,
        static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
    const int totalRows = widget.binding.param.empty()
        ? 0
        : 1 + static_cast<int>(std::count(widget.binding.param.begin(), widget.binding.param.end(), ','));
    const int visibleRows = rowHeight > 0
        ? std::clamp((std::max(0, static_cast<int>(widget.rect.bottom - widget.rect.top)) + rowHeight - 1) / rowHeight, 0, totalRows)
        : 0;

    const auto addGuide = [&](LayoutGuideAxis axis, int guideId, WidgetEditParameter parameter,
        const RECT& lineRect, const RECT& hitRect, int value, int dragDirection) {
        WidgetEditGuide guide;
        guide.axis = axis;
        guide.widget = LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.lineRect = lineRect;
        guide.hitRect = hitRect;
        guide.value = value;
        guide.dragDirection = dragDirection;
        widgetEditGuides_.push_back(std::move(guide));
    };

    addGuide(LayoutGuideAxis::Vertical, 0, WidgetEditParameter::MetricListLabelWidth,
        RECT{x, widget.rect.top, x + 1, widget.rect.bottom},
        RECT{x - hitInset, widget.rect.top, x + hitInset + 1, widget.rect.bottom},
        config_.layout.metricList.labelWidth, 1);

    for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
        const int y = widget.rect.top + ((rowIndex + 1) * rowHeight);
        addGuide(LayoutGuideAxis::Horizontal, 1 + rowIndex, WidgetEditParameter::MetricListVerticalGap,
            RECT{widget.rect.left, y, widget.rect.right, y + 1},
            RECT{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1},
            config_.layout.metricList.verticalGap, 1);
    }
}

void DashboardRenderer::AddDriveUsageWidgetEditGuides(const ResolvedWidgetLayout& widget) {
    const int headerHeight = EffectiveDriveHeaderHeight();
    const int rowHeight = EffectiveDriveRowHeight();
    const int labelWidth = std::max(1, measuredWidths_.driveLabel);
    const int percentWidth = std::max(1, measuredWidths_.drivePercent);
    const int freeWidth = std::max(1, ScaleLogical(config_.layout.driveUsageList.freeWidth));
    const int activityWidth = std::max(1, ScaleLogical(config_.layout.driveUsageList.activityWidth));
    const int barGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.barGap));
    const int valueGap = std::max(0, ScaleLogical(config_.layout.driveUsageList.valueGap));
    const int hitInset = std::max(3, ScaleLogical(4));
    const int totalRows = static_cast<int>(config_.storage.drives.size());
    const int availableRowPixels = std::max(0, static_cast<int>(widget.rect.bottom - widget.rect.top) - headerHeight);
    const int visibleRows = rowHeight > 0
        ? std::clamp((availableRowPixels + rowHeight - 1) / rowHeight, 0, totalRows)
        : 0;

    RECT labelRect{
        widget.rect.left,
        widget.rect.top,
        std::min(widget.rect.right, static_cast<LONG>(widget.rect.left + labelWidth)),
        widget.rect.bottom};
    RECT readRect{
        std::min(widget.rect.right, static_cast<LONG>(labelRect.right + barGap)),
        widget.rect.top,
        std::min(widget.rect.right, static_cast<LONG>(labelRect.right + barGap + activityWidth)),
        widget.rect.bottom};
    RECT writeRect{
        std::min(widget.rect.right, static_cast<LONG>(readRect.right + valueGap)),
        widget.rect.top,
        std::min(widget.rect.right, static_cast<LONG>(readRect.right + valueGap + activityWidth)),
        widget.rect.bottom};
    RECT freeRect{
        std::max(widget.rect.left, static_cast<LONG>(widget.rect.right - freeWidth)),
        widget.rect.top,
        widget.rect.right,
        widget.rect.bottom};
    RECT pctRect{
        std::max(widget.rect.left, static_cast<LONG>(freeRect.left - valueGap - percentWidth)),
        widget.rect.top,
        std::max(widget.rect.left, static_cast<LONG>(freeRect.left - valueGap)),
        widget.rect.bottom};
    if (pctRect.left < writeRect.right) {
        return;
    }

    const auto addVerticalGuide = [&](int guideId, int x, WidgetEditParameter parameter, int value, int dragDirection) {
        const int clampedX = std::clamp(x, static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
        WidgetEditGuide guide;
        guide.axis = LayoutGuideAxis::Vertical;
        guide.widget = LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.lineRect = RECT{clampedX, widget.rect.top, clampedX + 1, widget.rect.bottom};
        guide.hitRect = RECT{clampedX - hitInset, widget.rect.top, clampedX + hitInset + 1, widget.rect.bottom};
        guide.value = value;
        guide.dragDirection = dragDirection;
        widgetEditGuides_.push_back(std::move(guide));
    };
    const auto addHorizontalGuide = [&](int guideId, int y, WidgetEditParameter parameter, int value, int dragDirection) {
        const int clampedY = std::clamp(y, static_cast<int>(widget.rect.top), static_cast<int>(widget.rect.bottom));
        WidgetEditGuide guide;
        guide.axis = LayoutGuideAxis::Horizontal;
        guide.widget = LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.lineRect = RECT{widget.rect.left, clampedY, widget.rect.right, clampedY + 1};
        guide.hitRect = RECT{widget.rect.left, clampedY - hitInset, widget.rect.right, clampedY + hitInset + 1};
        guide.value = value;
        guide.dragDirection = dragDirection;
        widgetEditGuides_.push_back(std::move(guide));
    };

    addVerticalGuide(0, readRect.right, WidgetEditParameter::DriveUsageActivityWidth,
        config_.layout.driveUsageList.activityWidth, 1);
    addVerticalGuide(1, writeRect.right, WidgetEditParameter::DriveUsageActivityWidth,
        config_.layout.driveUsageList.activityWidth, 1);
    addVerticalGuide(2, freeRect.left, WidgetEditParameter::DriveUsageFreeWidth,
        config_.layout.driveUsageList.freeWidth, -1);
    addHorizontalGuide(3, widget.rect.top + headerHeight, WidgetEditParameter::DriveUsageHeaderGap,
        config_.layout.driveUsageList.headerGap, 1);
    for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
        const int y = widget.rect.top + headerHeight + ((rowIndex + 1) * rowHeight);
        addHorizontalGuide(4 + rowIndex, y, WidgetEditParameter::DriveUsageRowGap,
            config_.layout.driveUsageList.rowGap, 1);
    }
}

void DashboardRenderer::AddThroughputWidgetEditGuide(const ResolvedWidgetLayout& widget) {
    const int lineHeight = fontHeights_.smallText + std::max(0, ScaleLogical(config_.layout.throughput.valuePadding));
    RECT valueRect{widget.rect.left, widget.rect.top, widget.rect.right, std::min(widget.rect.bottom, widget.rect.top + lineHeight)};
    RECT graphRect{widget.rect.left, std::min(widget.rect.bottom, valueRect.bottom + std::max(0, ScaleLogical(config_.layout.throughput.headerGap))),
        widget.rect.right, widget.rect.bottom};
    const int axisWidth = std::max(1, measuredWidths_.throughputAxis);
    const int hitInset = std::max(3, ScaleLogical(4));
    const auto addGuide = [&](LayoutGuideAxis axis, int guideId, WidgetEditParameter parameter, const RECT& lineRect,
        const RECT& hitRect, int value, int dragDirection) {
        WidgetEditGuide guide;
        guide.axis = axis;
        guide.widget = LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.lineRect = lineRect;
        guide.hitRect = hitRect;
        guide.value = value;
        guide.dragDirection = dragDirection;
        widgetEditGuides_.push_back(std::move(guide));
    };

    const int x = std::clamp(static_cast<int>(graphRect.left) + axisWidth,
        static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
    addGuide(LayoutGuideAxis::Vertical, 0, WidgetEditParameter::ThroughputAxisPadding,
        RECT{x, graphRect.top, x + 1, graphRect.bottom},
        RECT{x - hitInset, graphRect.top, x + hitInset + 1, graphRect.bottom},
        config_.layout.throughput.axisPadding, 1);

    const int y = std::clamp(static_cast<int>(graphRect.top), static_cast<int>(widget.rect.top), static_cast<int>(widget.rect.bottom));
    addGuide(LayoutGuideAxis::Horizontal, 1, WidgetEditParameter::ThroughputHeaderGap,
        RECT{widget.rect.left, y, widget.rect.right, y + 1},
        RECT{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1},
        config_.layout.throughput.headerGap, 1);
}

void DashboardRenderer::BuildWidgetEditGuides() {
    widgetEditGuides_.clear();
    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.kind == WidgetKind::MetricList) {
                AddMetricListWidgetEditGuides(widget);
            }
            if (widget.kind == WidgetKind::Throughput) {
                AddThroughputWidgetEditGuide(widget);
            }
            if (widget.kind == WidgetKind::DriveUsageList) {
                AddDriveUsageWidgetEditGuides(widget);
            }
        }
    }
}

void DashboardRenderer::AddLayoutEditGuide(const LayoutNodeConfig& node, const RECT& rect, const std::vector<RECT>& childRects,
    int gap, const std::string& renderCardId, const std::string& editCardId, const std::vector<size_t>& nodePath) {
    if (!IsContainerNode(node) || childRects.size() < 2) {
        return;
    }

    const bool horizontal = node.name == "columns";
    const int hitInset = std::max(3, ScaleLogical(4));
    std::vector<bool> childFixedExtents;
    childFixedExtents.reserve(node.children.size());
    for (const auto& child : node.children) {
        const ResolvedWidgetLayout resolvedChild = ResolveWidgetLayout(child, RECT{});
        childFixedExtents.push_back(!horizontal &&
            (UsesFixedPreferredHeightInRows(resolvedChild) || resolvedChild.kind == WidgetKind::VerticalSpring));
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
            guide.childExtents.push_back(horizontal ? (childRect.right - childRect.left) : (childRect.bottom - childRect.top));
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

void DashboardRenderer::ResolveNodeWidgetsInternal(const LayoutNodeConfig& node, const RECT& rect,
    std::vector<ResolvedWidgetLayout>& widgets, std::vector<std::string>& cardReferenceStack,
    const std::string& renderCardId, const std::string& editCardId, const std::vector<size_t>& nodePath) {
    WriteTrace("renderer:layout_resolve_node name=\"" + node.name + "\" weight=" + std::to_string(node.weight) +
        " " + FormatRect(rect) + " children=" + std::to_string(node.children.size()));
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
        ResolveNodeWidgetsInternal(referencedCard->layout, rect, widgets, cardReferenceStack, renderCardId, node.name, {});
        cardReferenceStack.pop_back();
        return;
    }
    if (!IsContainerNode(node)) {
        ResolvedWidgetLayout widget = ResolveWidgetLayout(node, rect);
        widget.cardId = renderCardId;
        widget.editCardId = editCardId;
        widget.nodePath = nodePath;
        WriteTrace("renderer:layout_widget_resolved kind=\"" + node.name + "\" " + FormatRect(widget.rect) +
            (widget.binding.metric.empty() ? "" : " metric=\"" + widget.binding.metric + "\"") +
            (widget.binding.param.empty() ? "" : " param=\"" + widget.binding.param + "\""));
        widgets.push_back(std::move(widget));
        return;
    }

    const bool horizontal = node.name == "columns";
    const int gap = horizontal ? ScaleLogical(config_.layout.cardStyle.columnGap) : ScaleLogical(config_.layout.cardStyle.widgetLineGap);

    const int totalAvailable = (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
        gap * static_cast<int>(std::max<size_t>(0, node.children.size() - 1));
    int reservedPreferred = 0;
    int totalWeight = 0;
    int springWeight = 0;
    const bool rowsUseSprings = !horizontal && std::any_of(node.children.begin(), node.children.end(), [&](const auto& child) {
        return ResolveWidgetLayout(child, RECT{}).kind == WidgetKind::VerticalSpring;
    });
    if (!horizontal) {
        for (const auto& child : node.children) {
            const ResolvedWidgetLayout resolvedChild = ResolveWidgetLayout(child, RECT{});
            if (resolvedChild.kind == WidgetKind::VerticalSpring) {
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
        const ResolvedWidgetLayout resolvedChild = ResolveWidgetLayout(child, RECT{});
        const bool fixedPreferred = !horizontal && UsesFixedPreferredHeightInRows(resolvedChild);
        const bool verticalSpring = !horizontal && resolvedChild.kind == WidgetKind::VerticalSpring;
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
            "\" weight=" + std::to_string(childWeight) +
            " gap=" + std::to_string(gap) +
            " size=" + std::to_string(size) +
            " " + FormatRect(childRect));
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
    resolvedLayout_ = {};
    layoutEditGuides_.clear();
    widgetEditGuides_.clear();
    resolvedLayout_.windowWidth = WindowWidth();
    resolvedLayout_.windowHeight = WindowHeight();

    const RECT dashboardRect{
        ScaleLogical(config_.layout.dashboard.outerMargin),
        ScaleLogical(config_.layout.dashboard.outerMargin),
        WindowWidth() - ScaleLogical(config_.layout.dashboard.outerMargin),
        WindowHeight() - ScaleLogical(config_.layout.dashboard.outerMargin)
    };

    if (config_.layout.structure.cardsLayout.name.empty()) {
        lastError_ = "renderer:layout_missing_cards_root";
        return false;
    }

    WriteTrace("renderer:layout_begin window=" + std::to_string(resolvedLayout_.windowWidth) + "x" +
        std::to_string(resolvedLayout_.windowHeight) + " " + FormatRect(dashboardRect) +
        " cards_root=\"" + config_.layout.structure.cardsLayout.name + "\"");

    const auto resolveCard = [&](const LayoutNodeConfig& node, const RECT& rect) {
        const auto cardIt = std::find_if(config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) {
            return card.id == node.name;
        });
        if (cardIt == config_.layout.cards.end()) {
            return;
        }

        ResolvedCardLayout card;
        card.id = cardIt->id;
        card.title = cardIt->title;
        card.iconName = cardIt->icon;
        card.hasHeader = !card.title.empty() || !card.iconName.empty();
        card.rect = rect;

        const int padding = ScaleLogical(config_.layout.cardStyle.cardPadding);
        const int iconSize = ScaleLogical(config_.layout.cardStyle.headerIconSize);
        const int headerHeight = card.hasHeader ? EffectiveHeaderHeight() : 0;
        if (!card.iconName.empty()) {
            card.iconRect = RECT{
                card.rect.left + padding,
                card.rect.top + padding + std::max(0, (headerHeight - iconSize) / 2),
                card.rect.left + padding + iconSize,
                card.rect.top + padding + std::max(0, (headerHeight - iconSize) / 2) + iconSize
            };
        } else {
            card.iconRect = RECT{card.rect.left + padding, card.rect.top + padding, card.rect.left + padding, card.rect.top + padding};
        }
        const int titleLeft = !card.iconName.empty()
            ? card.iconRect.right + ScaleLogical(config_.layout.cardStyle.headerGap)
            : card.rect.left + padding;
        card.titleRect = RECT{
            titleLeft,
            card.rect.top + padding,
            card.rect.right - padding,
            card.rect.top + padding + headerHeight
        };
        card.contentRect = RECT{
            card.rect.left + padding,
            card.rect.top + padding + headerHeight + ScaleLogical(config_.layout.cardStyle.contentGap),
            card.rect.right - padding,
            card.rect.bottom - padding
        };

        WriteTrace("renderer:layout_card id=\"" + card.id + "\" " + FormatRect(card.rect) +
            " title=" + FormatRect(card.titleRect) +
            " icon=" + FormatRect(card.iconRect) +
            " content=" + FormatRect(card.contentRect));
        std::vector<std::string> cardReferenceStack;
        ResolveNodeWidgetsInternal(cardIt->layout, card.contentRect, card.widgets, cardReferenceStack, card.id, card.id, {});
        resolvedLayout_.cards.push_back(std::move(card));
    };

    std::function<void(const LayoutNodeConfig&, const RECT&, const std::vector<size_t>&)> resolveDashboardNode =
        [&](const LayoutNodeConfig& node, const RECT& rect, const std::vector<size_t>& nodePath) {
            if (!IsContainerNode(node)) {
                resolveCard(node, rect);
                return;
            }

            const bool horizontal = node.name == "columns";
            const int gap = horizontal ? ScaleLogical(config_.layout.dashboard.cardGap) : ScaleLogical(config_.layout.dashboard.rowGap);
            int totalWeight = 0;
            for (const auto& child : node.children) {
                totalWeight += std::max(1, child.weight);
            }
            if (totalWeight <= 0) {
                return;
            }

            const int totalAvailable = (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
                gap * static_cast<int>(std::max<size_t>(0, node.children.size() - 1));
            int remainingAvailable = totalAvailable;
            int cursor = horizontal ? rect.left : rect.top;
            int remainingWeight = totalWeight;
            std::vector<RECT> childRects;
            childRects.reserve(node.children.size());
            for (size_t i = 0; i < node.children.size(); ++i) {
                const auto& child = node.children[i];
                const int childWeight = std::max(1, child.weight);
                const int size = (i + 1 == node.children.size())
                    ? ((horizontal ? rect.right : rect.bottom) - cursor)
                    : std::max(0, remainingAvailable * childWeight / std::max(1, remainingWeight));

                RECT childRect = rect;
                if (horizontal) {
                    childRect.left = cursor;
                    childRect.right = cursor + size;
                } else {
                    childRect.top = cursor;
                    childRect.bottom = cursor + size;
                }

                WriteTrace("renderer:layout_dashboard_child parent=\"" + node.name + "\" child=\"" + child.name +
                    "\" weight=" + std::to_string(childWeight) +
                    " gap=" + std::to_string(gap) +
                    " size=" + std::to_string(size) +
                    " " + FormatRect(childRect));
                childRects.push_back(childRect);
                std::vector<size_t> childPath = nodePath;
                childPath.push_back(i);
                resolveDashboardNode(child, childRect, childPath);
                cursor += size + gap;
                remainingAvailable -= size;
                remainingWeight -= childWeight;
            }
            AddLayoutEditGuide(node, rect, childRects, gap, "", "", nodePath);
        };

    resolveDashboardNode(config_.layout.structure.cardsLayout, dashboardRect, {});

    if (resolvedLayout_.cards.empty()) {
        lastError_ = "renderer:layout_resolve_failed cards=0 root=\"" + config_.layout.structure.cardsLayout.name + "\"";
        return false;
    }

    int gaugeCount = 0;
    int globalGaugeRadius = 0;
    for (const auto& card : resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.kind != WidgetKind::Gauge) {
                continue;
            }

            const int gaugeRadius = GaugeRadiusForRect(widget.rect);
            if (gaugeCount == 0) {
                globalGaugeRadius = gaugeRadius;
            } else {
                globalGaugeRadius = std::min(globalGaugeRadius, gaugeRadius);
            }
            ++gaugeCount;
        }
    }
    resolvedLayout_.globalGaugeRadius = gaugeCount > 0 ? globalGaugeRadius :
        std::max(1, ScaleLogical(config_.layout.gauge.minRadius));
    WriteTrace("renderer:layout_global_gauge_radius count=" + std::to_string(gaugeCount) +
        " value=" + std::to_string(resolvedLayout_.globalGaugeRadius));

    BuildWidgetEditGuides();

    WriteTrace("renderer:layout_done cards=" + std::to_string(resolvedLayout_.cards.size()));
    return true;
}

