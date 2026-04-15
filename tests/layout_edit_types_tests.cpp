#include <gtest/gtest.h>

#include "layout_edit_types.h"

TEST(LayoutEditTypes, MatchesWidgetIdentityUsingKindAndPath) {
    const LayoutEditWidgetIdentity widgetA{"card-a", "card-a", {1, 2, 3}};
    const LayoutEditWidgetIdentity widgetB{"card-a", "card-a", {1, 2, 3}};
    const LayoutEditWidgetIdentity cardChrome{
        "card-a", "card-a", {1, 2, 3}, LayoutEditWidgetIdentity::Kind::CardChrome};

    EXPECT_TRUE(MatchesWidgetIdentity(widgetA, widgetB));
    EXPECT_FALSE(MatchesWidgetIdentity(widgetA, cardChrome));
}

TEST(LayoutEditTypes, TooltipPayloadHelpersTreatLayoutGuideAsNonParameterPayload) {
    LayoutEditGuide guide;
    guide.axis = LayoutGuideAxis::Horizontal;
    guide.editCardId = "cpu";
    guide.nodePath = {1, 0};
    guide.separatorIndex = 2;
    guide.lineRect = RenderRect{10, 20, 31, 24};

    const TooltipPayload payload = guide;

    EXPECT_TRUE(IsLayoutGuidePayload(payload));
    EXPECT_FALSE(TooltipPayloadParameter(payload).has_value());
    EXPECT_FALSE(TooltipPayloadNumericValue(payload).has_value());
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).x, 20);
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).y, 22);
    const auto focusKey = TooltipPayloadFocusKey(payload);
    ASSERT_TRUE(focusKey.has_value());
    const auto* focus = std::get_if<LayoutWeightEditKey>(&*focusKey);
    ASSERT_NE(focus, nullptr);
    EXPECT_EQ(focus->editCardId, "cpu");
    EXPECT_EQ(focus->nodePath, (std::vector<size_t>{1, 0}));
    EXPECT_EQ(focus->separatorIndex, 2u);
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
    const auto focusKey = TooltipPayloadFocusKey(payload);
    ASSERT_TRUE(focusKey.has_value());
    EXPECT_EQ(std::get<LayoutEditParameter>(*focusKey), LayoutEditParameter::MetricListRowGap);
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
    const auto focusKey = TooltipPayloadFocusKey(payload);
    ASSERT_TRUE(focusKey.has_value());
    EXPECT_EQ(std::get<LayoutEditParameter>(*focusKey), LayoutEditParameter::CardColumnGap);
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
    const auto focusKey = TooltipPayloadFocusKey(payload);
    ASSERT_TRUE(focusKey.has_value());
    EXPECT_EQ(std::get<LayoutEditParameter>(*focusKey), LayoutEditParameter::FontLabel);
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveColorRegions) {
    LayoutEditColorRegion region;
    region.parameter = LayoutEditParameter::ColorAccent;
    region.targetRect = RenderRect{10, 20, 50, 60};

    const TooltipPayload payload = region;

    ASSERT_TRUE(TooltipPayloadParameter(payload).has_value());
    EXPECT_EQ(*TooltipPayloadParameter(payload), LayoutEditParameter::ColorAccent);
    EXPECT_FALSE(TooltipPayloadNumericValue(payload).has_value());
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).x, 30);
    EXPECT_EQ(TooltipPayloadAnchorPoint(payload).y, 40);
    const auto focusKey = TooltipPayloadFocusKey(payload);
    ASSERT_TRUE(focusKey.has_value());
    EXPECT_EQ(std::get<LayoutEditParameter>(*focusKey), LayoutEditParameter::ColorAccent);
}

TEST(LayoutEditTypes, MatchesFocusKeysByParameterOrWeightIdentity) {
    const LayoutEditFocusKey parameterA = LayoutEditParameter::FontLabel;
    const LayoutEditFocusKey parameterB = LayoutEditParameter::FontLabel;
    const LayoutEditFocusKey weightA = LayoutWeightEditKey{"cpu", {1, 2}, 0};
    const LayoutEditFocusKey weightB = LayoutWeightEditKey{"cpu", {1, 2}, 0};
    const LayoutEditFocusKey weightC = LayoutWeightEditKey{"cpu", {1, 2}, 1};

    EXPECT_TRUE(MatchesLayoutEditFocusKey(parameterA, parameterB));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(weightA, weightB));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(parameterA, weightA));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(weightA, weightC));
}

TEST(LayoutEditTypes, MatchesSelectedParameterFocusAgainstWidgetAndAnchorArtifacts) {
    const LayoutEditFocusKey focusKey = LayoutEditParameter::FontLabel;

    LayoutEditWidgetGuide widgetGuide;
    widgetGuide.parameter = LayoutEditParameter::FontLabel;

    LayoutEditGapAnchorKey gapAnchorKey;
    gapAnchorKey.parameter = LayoutEditParameter::FontLabel;

    LayoutEditAnchorKey editableAnchorKey;
    editableAnchorKey.parameter = LayoutEditParameter::FontLabel;

    EXPECT_TRUE(MatchesLayoutEditFocusKey(focusKey, widgetGuide));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(focusKey, gapAnchorKey));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(focusKey, editableAnchorKey));
}

TEST(LayoutEditTypes, MatchesSelectedWeightFocusAgainstLayoutGuidesOnly) {
    const LayoutEditFocusKey focusKey = LayoutWeightEditKey{"gpu", {0, 1}, 2};

    LayoutEditGuide guide;
    guide.editCardId = "gpu";
    guide.nodePath = {0, 1};
    guide.separatorIndex = 2;

    LayoutEditWidgetGuide widgetGuide;
    widgetGuide.parameter = LayoutEditParameter::FontLabel;

    LayoutEditGapAnchorKey gapAnchorKey;
    gapAnchorKey.parameter = LayoutEditParameter::CardColumnGap;

    EXPECT_TRUE(MatchesLayoutEditFocusKey(focusKey, guide));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(focusKey, widgetGuide));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(focusKey, gapAnchorKey));
}

TEST(LayoutEditTypes, MatchesSelectionHighlightAgainstLeafAndContainerArtifacts) {
    const LayoutEditSelectionHighlight parameterHighlight = LayoutEditFocusKey{LayoutEditParameter::FontLabel};
    const LayoutEditSelectionHighlight containerHighlight = LayoutContainerEditKey{"gpu", {0, 1}};

    LayoutEditGuide guide;
    guide.editCardId = "gpu";
    guide.nodePath = {0, 1};
    guide.separatorIndex = 2;

    LayoutEditWidgetGuide widgetGuide;
    widgetGuide.parameter = LayoutEditParameter::FontLabel;

    LayoutEditAnchorKey editableAnchorKey;
    editableAnchorKey.parameter = LayoutEditParameter::FontLabel;

    EXPECT_TRUE(MatchesLayoutEditSelectionHighlight(parameterHighlight, widgetGuide));
    EXPECT_TRUE(MatchesLayoutEditSelectionHighlight(parameterHighlight, editableAnchorKey));
    EXPECT_FALSE(MatchesLayoutEditSelectionHighlight(containerHighlight, guide));
    EXPECT_FALSE(MatchesLayoutEditSelectionHighlight(containerHighlight, widgetGuide));
}

TEST(LayoutEditTypes, MatchesCardChromeSelectionByEditedCardIdentity) {
    const LayoutEditWidgetIdentity selection{
        "storage_throughput", "storage_throughput", {}, LayoutEditWidgetIdentity::Kind::CardChrome};
    const LayoutEditWidgetIdentity topLevelCandidate{
        "storage_throughput", "storage_throughput", {}, LayoutEditWidgetIdentity::Kind::CardChrome};
    const LayoutEditWidgetIdentity embeddedCandidate{
        "storage", "storage_throughput", {}, LayoutEditWidgetIdentity::Kind::CardChrome};
    const LayoutEditWidgetIdentity wrongCardCandidate{
        "storage", "storage", {}, LayoutEditWidgetIdentity::Kind::CardChrome};
    const LayoutEditWidgetIdentity widgetCandidate{
        "storage", "storage_throughput", {}, LayoutEditWidgetIdentity::Kind::Widget};

    EXPECT_TRUE(MatchesCardChromeSelectionIdentity(selection, topLevelCandidate));
    EXPECT_TRUE(MatchesCardChromeSelectionIdentity(selection, embeddedCandidate));
    EXPECT_FALSE(MatchesCardChromeSelectionIdentity(selection, wrongCardCandidate));
    EXPECT_FALSE(MatchesCardChromeSelectionIdentity(selection, widgetCandidate));
}
