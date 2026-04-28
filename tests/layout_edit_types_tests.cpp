#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_tooltip_payload.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_hit_priority.h"

namespace {

LayoutNodeConfig MakeNode(std::string name, std::string parameter = {}) {
    LayoutNodeConfig node;
    node.name = std::move(name);
    node.parameter = std::move(parameter);
    return node;
}

}  // namespace

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
    anchor.key.subject = LayoutEditParameter::FontLabel;
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

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveMetricEditableAnchorFocus) {
    LayoutEditAnchorRegion anchor;
    anchor.key.widget = {"card-a", "card-a", {0}};
    anchor.key.subject = LayoutMetricEditKey{"gpu.temp"};
    anchor.anchorRect = RenderRect{100, 200, 109, 209};

    const TooltipPayload payload = anchor;

    EXPECT_FALSE(TooltipPayloadParameter(payload).has_value());
    EXPECT_FALSE(TooltipPayloadNumericValue(payload).has_value());
    const auto focusKey = TooltipPayloadFocusKey(payload);
    ASSERT_TRUE(focusKey.has_value());
    const auto* metricKey = std::get_if<LayoutMetricEditKey>(&*focusKey);
    ASSERT_NE(metricKey, nullptr);
    EXPECT_EQ(metricKey->metricId, "gpu.temp");
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveCardTitleEditableAnchorFocus) {
    LayoutEditAnchorRegion anchor;
    anchor.key.widget = {"card-a", "card-a", {}, LayoutEditWidgetIdentity::Kind::CardChrome};
    anchor.key.subject = LayoutCardTitleEditKey{"card-a"};
    anchor.anchorRect = RenderRect{100, 200, 109, 209};

    const TooltipPayload payload = anchor;

    EXPECT_FALSE(TooltipPayloadParameter(payload).has_value());
    EXPECT_FALSE(TooltipPayloadNumericValue(payload).has_value());
    const auto focusKey = TooltipPayloadFocusKey(payload);
    ASSERT_TRUE(focusKey.has_value());
    const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&*focusKey);
    ASSERT_NE(cardTitleKey, nullptr);
    EXPECT_EQ(cardTitleKey->cardId, "card-a");
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveMetricListEditableAnchorFocus) {
    LayoutEditAnchorRegion anchor;
    anchor.key.widget = {"card-a", "card-a", {0}};
    anchor.key.subject = LayoutNodeFieldEditKey{"card-a", {0}, WidgetClass::MetricList, LayoutNodeField::Parameter};
    anchor.anchorRect = RenderRect{100, 200, 109, 209};

    const TooltipPayload payload = anchor;

    EXPECT_FALSE(TooltipPayloadParameter(payload).has_value());
    EXPECT_FALSE(TooltipPayloadNumericValue(payload).has_value());
    const auto focusKey = TooltipPayloadFocusKey(payload);
    ASSERT_TRUE(focusKey.has_value());
    const auto* metricListKey = std::get_if<LayoutNodeFieldEditKey>(&*focusKey);
    ASSERT_NE(metricListKey, nullptr);
    EXPECT_EQ(metricListKey->editCardId, "card-a");
    EXPECT_EQ(metricListKey->nodePath, (std::vector<size_t>{0}));
    EXPECT_EQ(metricListKey->widgetClass, WidgetClass::MetricList);
    EXPECT_EQ(metricListKey->field, LayoutNodeField::Parameter);
}

TEST(LayoutEditTypes, TooltipPayloadHelpersResolveDateTimeNodeFieldEditableAnchorFocus) {
    LayoutEditAnchorRegion anchor;
    anchor.key.widget = {"card-a", "card-a", {1}};
    anchor.key.subject = LayoutNodeFieldEditKey{"card-a", {1}, WidgetClass::ClockDate, LayoutNodeField::Parameter};
    anchor.anchorRect = RenderRect{100, 200, 109, 209};

    const TooltipPayload payload = anchor;

    EXPECT_FALSE(TooltipPayloadParameter(payload).has_value());
    EXPECT_FALSE(TooltipPayloadNumericValue(payload).has_value());
    const auto focusKey = TooltipPayloadFocusKey(payload);
    ASSERT_TRUE(focusKey.has_value());
    const auto* formatKey = std::get_if<LayoutNodeFieldEditKey>(&*focusKey);
    ASSERT_NE(formatKey, nullptr);
    EXPECT_EQ(formatKey->editCardId, "card-a");
    EXPECT_EQ(formatKey->nodePath, (std::vector<size_t>{1}));
    EXPECT_EQ(formatKey->widgetClass, WidgetClass::ClockDate);
    EXPECT_EQ(formatKey->field, LayoutNodeField::Parameter);
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
    const LayoutEditFocusKey metricA = LayoutMetricEditKey{"gpu.temp"};
    const LayoutEditFocusKey metricB = LayoutMetricEditKey{"gpu.temp"};
    const LayoutEditFocusKey metricC = LayoutMetricEditKey{"cpu.load"};
    const LayoutEditFocusKey cardTitleA = LayoutCardTitleEditKey{"gpu"};
    const LayoutEditFocusKey cardTitleB = LayoutCardTitleEditKey{"gpu"};
    const LayoutEditFocusKey cardTitleC = LayoutCardTitleEditKey{"cpu"};
    const LayoutEditFocusKey metricListA =
        LayoutNodeFieldEditKey{"gpu", {0, 1}, WidgetClass::MetricList, LayoutNodeField::Parameter};
    const LayoutEditFocusKey metricListB =
        LayoutNodeFieldEditKey{"gpu", {0, 1}, WidgetClass::MetricList, LayoutNodeField::Parameter};
    const LayoutEditFocusKey metricListC =
        LayoutNodeFieldEditKey{"gpu", {1, 0}, WidgetClass::MetricList, LayoutNodeField::Parameter};
    const LayoutEditFocusKey formatA =
        LayoutNodeFieldEditKey{"gpu", {2}, WidgetClass::ClockTime, LayoutNodeField::Parameter};
    const LayoutEditFocusKey formatB =
        LayoutNodeFieldEditKey{"gpu", {2}, WidgetClass::ClockTime, LayoutNodeField::Parameter};
    const LayoutEditFocusKey formatC =
        LayoutNodeFieldEditKey{"gpu", {2}, WidgetClass::ClockDate, LayoutNodeField::Parameter};

    EXPECT_TRUE(MatchesLayoutEditFocusKey(parameterA, parameterB));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(weightA, weightB));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(metricA, metricB));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(cardTitleA, cardTitleB));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(metricListA, metricListB));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(formatA, formatB));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(parameterA, weightA));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(weightA, weightC));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(metricA, metricC));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(cardTitleA, cardTitleC));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(metricListA, metricListC));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(formatA, formatC));
}

TEST(LayoutEditTypes, MatchesSelectedParameterFocusAgainstWidgetAndAnchorArtifacts) {
    const LayoutEditFocusKey focusKey = LayoutEditParameter::FontLabel;

    LayoutEditWidgetGuide widgetGuide;
    widgetGuide.parameter = LayoutEditParameter::FontLabel;

    LayoutEditGapAnchorKey gapAnchorKey;
    gapAnchorKey.parameter = LayoutEditParameter::FontLabel;

    LayoutEditAnchorKey editableAnchorKey;
    editableAnchorKey.subject = LayoutEditParameter::FontLabel;

    EXPECT_TRUE(MatchesLayoutEditFocusKey(focusKey, widgetGuide));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(focusKey, gapAnchorKey));
    EXPECT_TRUE(MatchesLayoutEditFocusKey(focusKey, editableAnchorKey));
}

TEST(LayoutEditTypes, PrioritizesMetricAndTitleAnchorsAboveGuides) {
    LayoutEditAnchorKey metricAnchor;
    metricAnchor.subject = LayoutMetricEditKey{"board.fan.system"};

    LayoutEditAnchorKey titleAnchor;
    titleAnchor.subject = LayoutCardTitleEditKey{"card-a"};

    EXPECT_LT(LayoutEditAnchorHitPriority(metricAnchor),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::MetricListLabelWidth));
    EXPECT_LT(LayoutEditAnchorHitPriority(metricAnchor),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::TextBottomGap));
    EXPECT_LT(LayoutEditAnchorHitPriority(titleAnchor),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::CardHeaderContentGap));
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

    LayoutEditAnchorKey editableAnchorKey;
    editableAnchorKey.subject = LayoutMetricEditKey{"gpu.temp"};

    EXPECT_TRUE(MatchesLayoutEditFocusKey(focusKey, guide));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(focusKey, widgetGuide));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(focusKey, gapAnchorKey));
    EXPECT_FALSE(MatchesLayoutEditFocusKey(focusKey, editableAnchorKey));
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
    editableAnchorKey.subject = LayoutEditParameter::FontLabel;

    EXPECT_TRUE(MatchesLayoutEditSelectionHighlight(parameterHighlight, widgetGuide));
    EXPECT_TRUE(MatchesLayoutEditSelectionHighlight(parameterHighlight, editableAnchorKey));
    EXPECT_FALSE(MatchesLayoutEditSelectionHighlight(containerHighlight, guide));
    EXPECT_FALSE(MatchesLayoutEditSelectionHighlight(containerHighlight, widgetGuide));
}

TEST(LayoutEditTypes, MatchesMetricSelectionHighlightAgainstEditableAnchors) {
    const LayoutEditSelectionHighlight metricHighlight = LayoutEditFocusKey{LayoutMetricEditKey{"gpu.temp"}};

    LayoutEditAnchorKey metricAnchorKey;
    metricAnchorKey.subject = LayoutMetricEditKey{"gpu.temp"};

    LayoutEditAnchorKey otherAnchorKey;
    otherAnchorKey.subject = LayoutMetricEditKey{"cpu.load"};

    EXPECT_TRUE(MatchesLayoutEditSelectionHighlight(metricHighlight, metricAnchorKey));
    EXPECT_FALSE(MatchesLayoutEditSelectionHighlight(metricHighlight, otherAnchorKey));
}

TEST(LayoutEditTypes, MatchesCardTitleSelectionHighlightAgainstEditableAnchors) {
    const LayoutEditSelectionHighlight cardTitleHighlight = LayoutEditFocusKey{LayoutCardTitleEditKey{"gpu"}};

    LayoutEditAnchorKey titleAnchorKey;
    titleAnchorKey.subject = LayoutCardTitleEditKey{"gpu"};

    LayoutEditAnchorKey otherAnchorKey;
    otherAnchorKey.subject = LayoutCardTitleEditKey{"cpu"};

    EXPECT_TRUE(MatchesLayoutEditSelectionHighlight(cardTitleHighlight, titleAnchorKey));
    EXPECT_FALSE(MatchesLayoutEditSelectionHighlight(cardTitleHighlight, otherAnchorKey));
}

TEST(LayoutEditTypes, MatchesMetricListSelectionHighlightAgainstEditableAnchors) {
    const LayoutEditSelectionHighlight metricListHighlight =
        LayoutEditFocusKey{LayoutNodeFieldEditKey{"gpu", {0, 1}, WidgetClass::MetricList, LayoutNodeField::Parameter}};

    LayoutEditAnchorKey metricListAnchorKey;
    metricListAnchorKey.subject =
        LayoutNodeFieldEditKey{"gpu", {0, 1}, WidgetClass::MetricList, LayoutNodeField::Parameter};

    LayoutEditAnchorKey otherAnchorKey;
    otherAnchorKey.subject = LayoutNodeFieldEditKey{"gpu", {1, 0}, WidgetClass::MetricList, LayoutNodeField::Parameter};

    EXPECT_TRUE(MatchesLayoutEditSelectionHighlight(metricListHighlight, metricListAnchorKey));
    EXPECT_FALSE(MatchesLayoutEditSelectionHighlight(metricListHighlight, otherAnchorKey));
}

TEST(LayoutEditService, DescribesWidgetNodeParameterEditors) {
    const LayoutNodeFieldEditKey metricListKey{"gpu", {0, 1}, WidgetClass::MetricList, LayoutNodeField::Parameter};
    const LayoutNodeFieldEditKey timeKey{"time", {0}, WidgetClass::ClockTime, LayoutNodeField::Parameter};

    const LayoutNodeFieldEditDescriptor* metricList = FindLayoutNodeFieldEditDescriptor(metricListKey);
    ASSERT_NE(metricList, nullptr);
    EXPECT_EQ(metricList->editorKind, LayoutEditEditorKind::MetricListOrder);
    EXPECT_EQ(metricList->label, "metric_list");

    const LayoutNodeFieldEditDescriptor* time = FindLayoutNodeFieldEditDescriptor(timeKey);
    ASSERT_NE(time, nullptr);
    EXPECT_EQ(time->editorKind, LayoutEditEditorKind::DateTimeFormat);
    EXPECT_EQ(time->label, "clock_time");
}

TEST(LayoutEditService, AppliesNodeFieldPreviewToDashboardAndActiveNamedLayout) {
    AppConfig config;
    config.display.layout = "main";
    config.layout.structure.cardsLayout.name = "rows";
    config.layout.structure.cardsLayout.children.push_back(MakeNode("metric_list", "cpu.load"));
    LayoutSectionConfig named;
    named.name = "main";
    named.cardsLayout.name = "rows";
    named.cardsLayout.children.push_back(MakeNode("metric_list", "cpu.load"));
    config.layout.layouts.push_back(named);

    const LayoutEditFocusKey key = LayoutNodeFieldEditKey{"", {0}, WidgetClass::MetricList, LayoutNodeField::Parameter};

    ASSERT_TRUE(ApplyLayoutEditValue(config, key, LayoutEditValue{std::vector<std::string>{"cpu.ram", "gpu.vram"}}));
    EXPECT_EQ(config.layout.structure.cardsLayout.children[0].parameter, "cpu.ram,gpu.vram");
    EXPECT_EQ(config.layout.layouts[0].cardsLayout.children[0].parameter, "cpu.ram,gpu.vram");
}

TEST(LayoutEditService, AppliesNodeFieldPreviewToCardLayout) {
    AppConfig config;
    LayoutCardConfig card;
    card.id = "time";
    card.layout.name = "rows";
    card.layout.children.push_back(MakeNode("clock_date", "YYYY-MM-DD"));
    config.layout.cards.push_back(card);

    const LayoutEditFocusKey key =
        LayoutNodeFieldEditKey{"time", {0}, WidgetClass::ClockDate, LayoutNodeField::Parameter};

    ASSERT_TRUE(ApplyLayoutEditValue(config, key, LayoutEditValue{std::string("DD.MM.YYYY")}));
    EXPECT_EQ(config.layout.cards[0].layout.children[0].parameter, "DD.MM.YYYY");
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
