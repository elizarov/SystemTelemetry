#include <gtest/gtest.h>

#include "layout_edit_types.h"

TEST(LayoutEditTypes, MatchesWidgetIdentityUsingKindAndPath) {
    const LayoutEditWidgetIdentity widgetA{"card-a", "card-a", {1, 2, 3}};
    const LayoutEditWidgetIdentity widgetB{"card-a", "card-a", {1, 2, 3}};
    const LayoutEditWidgetIdentity cardChrome{"card-a", "card-a", {1, 2, 3}, LayoutEditWidgetIdentity::Kind::CardChrome};

    EXPECT_TRUE(MatchesWidgetIdentity(widgetA, widgetB));
    EXPECT_FALSE(MatchesWidgetIdentity(widgetA, cardChrome));
}

TEST(LayoutEditTypes, TooltipPayloadHelpersTreatLayoutGuideAsNonParameterPayload) {
    LayoutEditGuide guide;
    guide.axis = LayoutGuideAxis::Horizontal;
    guide.lineRect = RenderRect{10, 20, 31, 24};

    const TooltipPayload payload = guide;

    EXPECT_TRUE(IsLayoutGuidePayload(payload));
    EXPECT_FALSE(TooltipPayloadParameter(payload).has_value());
    EXPECT_FALSE(TooltipPayloadNumericValue(payload).has_value());
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).x, 20);
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).y, 22);
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveWidgetGuideValues) {
    LayoutEditWidgetGuide guide;
    guide.widget = {"card-a", "card-a", {2, 1}};
    guide.parameter = LayoutEditParameter::MetricListRowGap;
    guide.drawEnd = RenderPoint{70, 90};
    guide.value = 18.0;

    const TooltipPayload payload = guide;

    ASSERT_TRUE(TooltipPayloadParameter(payload).has_value());
    EXPECT_EQ(*TooltipPayloadParameter(payload), LayoutEditParameter::MetricListRowGap);
    ASSERT_TRUE(TooltipPayloadNumericValue(payload).has_value());
    EXPECT_DOUBLE_EQ(*TooltipPayloadNumericValue(payload), 18.0);
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).x, 70);
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).y, 90);
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveGapAnchorValues) {
    LayoutEditGapAnchor anchor;
    anchor.key.widget = {"card-a", "card-a", {}};
    anchor.key.parameter = LayoutEditParameter::CardColumnGap;
    anchor.handleRect = RenderRect{40, 10, 50, 20};
    anchor.value = 12.0;

    const TooltipPayload payload = anchor;

    ASSERT_TRUE(TooltipPayloadParameter(payload).has_value());
    EXPECT_EQ(*TooltipPayloadParameter(payload), LayoutEditParameter::CardColumnGap);
    ASSERT_TRUE(TooltipPayloadNumericValue(payload).has_value());
    EXPECT_DOUBLE_EQ(*TooltipPayloadNumericValue(payload), 12.0);
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).x, 45);
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).y, 15);
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveEditableAnchorValues) {
    LayoutEditAnchorRegion anchor;
    anchor.key.widget = {"card-a", "card-a", {0}};
    anchor.key.parameter = LayoutEditParameter::FontLabel;
    anchor.anchorRect = RenderRect{100, 200, 109, 209};
    anchor.value = 17;

    const TooltipPayload payload = anchor;

    ASSERT_TRUE(TooltipPayloadParameter(payload).has_value());
    EXPECT_EQ(*TooltipPayloadParameter(payload), LayoutEditParameter::FontLabel);
    ASSERT_TRUE(TooltipPayloadNumericValue(payload).has_value());
    EXPECT_DOUBLE_EQ(*TooltipPayloadNumericValue(payload), 17.0);
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).x, 104);
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).y, 204);
}
