#include "widget/card_chrome_layout.h"

#include <algorithm>

#include "widget/widget_renderer.h"

CardChromeLayoutMetrics ResolveCardChromeLayoutMetrics(const WidgetRenderer& renderer) {
    const auto& style = renderer.Config().layout.cardStyle;
    CardChromeLayoutMetrics metrics;
    metrics.padding = (std::max)(0, renderer.ScaleLogical(style.cardPadding));
    metrics.iconSize = (std::max)(0, renderer.ScaleLogical(style.headerIconSize));
    metrics.iconGap = (std::max)(0, renderer.ScaleLogical(style.headerIconGap));
    metrics.headerContentGap = (std::max)(0, renderer.ScaleLogical(style.headerContentGap));
    metrics.titleHeight = (std::max)(0, renderer.TextMetrics().title);
    return metrics;
}

CardChromeLayout ResolveCardChromeLayout(
    const LayoutCardConfig& card, const RenderRect& cardRect, const CardChromeLayoutMetrics& metrics) {
    return ResolveCardChromeLayout(card.title, card.icon, cardRect, metrics);
}

CardChromeLayout ResolveCardChromeLayout(std::string_view title,
    std::string_view iconName,
    const RenderRect& cardRect,
    const CardChromeLayoutMetrics& metrics) {
    CardChromeLayout layout;
    layout.hasHeader = !title.empty() || !iconName.empty();

    const int headerHeight = layout.hasHeader ? (std::max)(metrics.titleHeight, metrics.iconSize) : 0;
    const int padding = (std::max)(0, metrics.padding);
    const int iconSize = (std::max)(0, metrics.iconSize);

    if (!iconName.empty()) {
        layout.iconRect = RenderRect{cardRect.left + padding,
            cardRect.top + padding + (std::max)(0, (headerHeight - iconSize) / 2),
            cardRect.left + padding + iconSize,
            cardRect.top + padding + (std::max)(0, (headerHeight - iconSize) / 2) + iconSize};
    } else {
        layout.iconRect = RenderRect{
            cardRect.left + padding, cardRect.top + padding, cardRect.left + padding, cardRect.top + padding};
    }

    const int titleLeft =
        !iconName.empty() ? layout.iconRect.right + (std::max)(0, metrics.iconGap) : cardRect.left + padding;
    layout.titleRect =
        RenderRect{titleLeft, cardRect.top + padding, cardRect.right - padding, cardRect.top + padding + headerHeight};
    layout.contentRect = RenderRect{cardRect.left + padding,
        cardRect.top + padding + headerHeight + (layout.hasHeader ? (std::max)(0, metrics.headerContentGap) : 0),
        cardRect.right - padding,
        cardRect.bottom - padding};
    return layout;
}
