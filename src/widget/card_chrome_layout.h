#pragma once

#include <string_view>

#include "config/config.h"
#include "widget/render_types.h"

class WidgetRenderer;

struct CardChromeLayoutMetrics {
    int padding = 0;
    int iconSize = 0;
    int iconGap = 0;
    int headerContentGap = 0;
    int titleHeight = 0;
};

struct CardChromeLayout {
    bool hasHeader = false;
    RenderRect iconRect{};
    RenderRect titleRect{};
    RenderRect contentRect{};
};

CardChromeLayoutMetrics ResolveCardChromeLayoutMetrics(const WidgetRenderer& renderer);
CardChromeLayout ResolveCardChromeLayout(
    const LayoutCardConfig& card, const RenderRect& cardRect, const CardChromeLayoutMetrics& metrics);
CardChromeLayout ResolveCardChromeLayout(std::string_view title,
    std::string_view iconName,
    const RenderRect& cardRect,
    const CardChromeLayoutMetrics& metrics);
