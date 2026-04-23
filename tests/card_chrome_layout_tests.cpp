#include <gtest/gtest.h>

#include "widget/card_chrome_layout.h"

namespace {

void ExpectRect(const RenderRect& rect, int left, int top, int right, int bottom) {
    EXPECT_EQ(rect.left, left);
    EXPECT_EQ(rect.top, top);
    EXPECT_EQ(rect.right, right);
    EXPECT_EQ(rect.bottom, bottom);
}

CardChromeLayoutMetrics MakeMetrics() {
    CardChromeLayoutMetrics metrics;
    metrics.padding = 12;
    metrics.iconSize = 20;
    metrics.iconGap = 7;
    metrics.headerContentGap = 5;
    metrics.titleHeight = 18;
    return metrics;
}

LayoutCardConfig MakeCard(std::string_view title, std::string_view icon) {
    LayoutCardConfig card;
    card.id = "alpha";
    card.title = std::string(title);
    card.icon = std::string(icon);
    return card;
}

}  // namespace

TEST(CardChromeLayout, ComputesContentRectWithoutHeader) {
    const CardChromeLayout layout = ResolveCardChromeLayout(MakeCard("", ""), RenderRect{0, 0, 100, 80}, MakeMetrics());

    EXPECT_FALSE(layout.hasHeader);
    ExpectRect(layout.iconRect, 12, 12, 12, 12);
    ExpectRect(layout.titleRect, 12, 12, 88, 12);
    ExpectRect(layout.contentRect, 12, 12, 88, 68);
}

TEST(CardChromeLayout, ComputesGeometryForTitleOnlyHeader) {
    const CardChromeLayout layout =
        ResolveCardChromeLayout(MakeCard("CPU", ""), RenderRect{0, 0, 100, 80}, MakeMetrics());

    EXPECT_TRUE(layout.hasHeader);
    ExpectRect(layout.iconRect, 12, 12, 12, 12);
    ExpectRect(layout.titleRect, 12, 12, 88, 32);
    ExpectRect(layout.contentRect, 12, 37, 88, 68);
}

TEST(CardChromeLayout, ComputesGeometryForIconOnlyHeader) {
    const CardChromeLayout layout =
        ResolveCardChromeLayout(MakeCard("", "cpu"), RenderRect{0, 0, 100, 80}, MakeMetrics());

    EXPECT_TRUE(layout.hasHeader);
    ExpectRect(layout.iconRect, 12, 12, 32, 32);
    ExpectRect(layout.titleRect, 39, 12, 88, 32);
    ExpectRect(layout.contentRect, 12, 37, 88, 68);
}

TEST(CardChromeLayout, ComputesGeometryForIconAndTitleHeader) {
    const CardChromeLayout layout =
        ResolveCardChromeLayout(MakeCard("CPU", "cpu"), RenderRect{0, 0, 100, 80}, MakeMetrics());

    EXPECT_TRUE(layout.hasHeader);
    ExpectRect(layout.iconRect, 12, 12, 32, 32);
    ExpectRect(layout.titleRect, 39, 12, 88, 32);
    ExpectRect(layout.contentRect, 12, 37, 88, 68);
}
