#include <gtest/gtest.h>

#include "layout_edit_types.h"

TEST(LayoutEditTypes, MatchesWidgetIdentityUsingKindAndPath) {
    const layout_edit::LayoutEditWidgetIdentity widgetA{"card-a", "card-a", {1, 2, 3}};
    const layout_edit::LayoutEditWidgetIdentity widgetB{"card-a", "card-a", {1, 2, 3}};
    const layout_edit::LayoutEditWidgetIdentity cardChrome{
        "card-a", "card-a", {1, 2, 3}, layout_edit::LayoutEditWidgetIdentity::Kind::CardChrome};

    EXPECT_TRUE(layout_edit::MatchesWidgetIdentity(widgetA, widgetB));
    EXPECT_FALSE(layout_edit::MatchesWidgetIdentity(widgetA, cardChrome));
}

TEST(LayoutEditTypes, TooltipPayloadHelpersTreatLayoutGuideAsNonParameterPayload) {
    layout_edit::LayoutEditGuide guide;
    guide.axis = layout_edit::LayoutGuideAxis::Horizontal;
    guide.lineRect = RenderRect{10, 20, 31, 24};

    const layout_edit::TooltipPayload payload = guide;

    EXPECT_TRUE(layout_edit::IsLayoutGuidePayload(payload));
    EXPECT_FALSE(layout_edit::TooltipPayloadParameter(payload).has_value());
    EXPECT_FALSE(layout_edit::TooltipPayloadNumericValue(payload).has_value());
    EXPECT_EQ(layout_edit::TooltipPayloadAnchorPoint(payload).x, 20);
    EXPECT_EQ(layout_edit::TooltipPayloadAnchorPoint(payload).y, 22);
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveWidgetGuideValues) {
    layout_edit::LayoutEditWidgetGuide guide;
    guide.widget = {"card-a", "card-a", {2, 1}};
    guide.parameter = LayoutEditParameter::MetricListRowGap;
    guide.drawEnd = RenderPoint{70, 90};
    guide.value = 18.0;

    const layout_edit::TooltipPayload payload = guide;

    ASSERT_TRUE(layout_edit::TooltipPayloadParameter(payload).has_value());
    EXPECT_EQ(*layout_edit::TooltipPayloadParameter(payload), LayoutEditParameter::MetricListRowGap);
    ASSERT_TRUE(layout_edit::TooltipPayloadNumericValue(payload).has_value());
    EXPECT_DOUBLE_EQ(*layout_edit::TooltipPayloadNumericValue(payload), 18.0);
    EXPECT_EQ(layout_edit::TooltipPayloadAnchorPoint(payload).x, 70);
    EXPECT_EQ(layout_edit::TooltipPayloadAnchorPoint(payload).y, 90);
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveGapAnchorValues) {
    layout_edit::LayoutEditGapAnchor anchor;
    anchor.key.widget = {"card-a", "card-a", {}};
    anchor.key.parameter = LayoutEditParameter::CardColumnGap;
    anchor.handleRect = RenderRect{40, 10, 50, 20};
    anchor.value = 12.0;

    const layout_edit::TooltipPayload payload = anchor;

    ASSERT_TRUE(layout_edit::TooltipPayloadParameter(payload).has_value());
    EXPECT_EQ(*layout_edit::TooltipPayloadParameter(payload), LayoutEditParameter::CardColumnGap);
    ASSERT_TRUE(layout_edit::TooltipPayloadNumericValue(payload).has_value());
    EXPECT_DOUBLE_EQ(*layout_edit::TooltipPayloadNumericValue(payload), 12.0);
    EXPECT_EQ(layout_edit::TooltipPayloadAnchorPoint(payload).x, 45);
    EXPECT_EQ(layout_edit::TooltipPayloadAnchorPoint(payload).y, 15);
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveEditableAnchorValues) {
    layout_edit::LayoutEditAnchorRegion anchor;
    anchor.key.widget = {"card-a", "card-a", {0}};
    anchor.key.parameter = LayoutEditParameter::FontLabel;
    anchor.anchorRect = RenderRect{100, 200, 109, 209};
    anchor.value = 17;

    const layout_edit::TooltipPayload payload = anchor;

    ASSERT_TRUE(layout_edit::TooltipPayloadParameter(payload).has_value());
    EXPECT_EQ(*layout_edit::TooltipPayloadParameter(payload), LayoutEditParameter::FontLabel);
    ASSERT_TRUE(layout_edit::TooltipPayloadNumericValue(payload).has_value());
    EXPECT_DOUBLE_EQ(*layout_edit::TooltipPayloadNumericValue(payload), 17.0);
    EXPECT_EQ(layout_edit::TooltipPayloadAnchorPoint(payload).x, 104);
    EXPECT_EQ(layout_edit::TooltipPayloadAnchorPoint(payload).y, 204);
}
