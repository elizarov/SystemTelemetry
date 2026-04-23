#include <gtest/gtest.h>

#include "widget/widget.h"

TEST(WidgetFactory, CreatesCardChromeSeparatelyFromEnumWidgets) {
    LayoutCardConfig card;
    card.id = "alpha";
    card.title = "CPU";
    card.icon = "cpu";

    std::unique_ptr<Widget> chrome = CreateCardChromeWidget(card);
    ASSERT_NE(chrome, nullptr);
    EXPECT_EQ(chrome->Class(), WidgetClass::Unknown);

    EXPECT_NE(CreateWidget("gauge"), nullptr);
    EXPECT_EQ(CreateWidget("card_chrome"), nullptr);
}
