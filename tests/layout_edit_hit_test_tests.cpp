#include <gtest/gtest.h>

#include "layout_edit/layout_edit_hit_test.h"

namespace {

LayoutEditActiveRegion AnchorHandleRegion(LayoutEditAnchorRegion anchor) {
    return LayoutEditActiveRegion{
        anchor.anchorHitRect, LayoutEditActiveRegionKind::StaticEditAnchorHandle, std::move(anchor)};
}

LayoutEditActiveRegion AnchorTargetRegion(LayoutEditAnchorRegion anchor) {
    return LayoutEditActiveRegion{
        anchor.targetRect, LayoutEditActiveRegionKind::StaticEditAnchorTarget, std::move(anchor)};
}

LayoutEditActiveRegion GapRegion(LayoutEditGapAnchor anchor) {
    return LayoutEditActiveRegion{anchor.hitRect, LayoutEditActiveRegionKind::GapHandle, std::move(anchor)};
}

LayoutEditActiveRegion WidgetGuideRegion(LayoutEditWidgetGuide guide) {
    return LayoutEditActiveRegion{guide.hitRect, LayoutEditActiveRegionKind::WidgetGuide, std::move(guide)};
}

LayoutEditActiveRegion LayoutGuideRegion(LayoutEditGuide guide) {
    return LayoutEditActiveRegion{guide.hitRect, LayoutEditActiveRegionKind::LayoutWeightGuide, std::move(guide)};
}

LayoutEditActiveRegion ColorRegion(LayoutEditColorRegion color) {
    return LayoutEditActiveRegion{color.targetRect, LayoutEditActiveRegionKind::StaticColorTarget, color};
}

LayoutEditAnchorRegion BasicAnchor(LayoutEditParameter parameter, RenderRect rect) {
    LayoutEditAnchorRegion anchor;
    anchor.key.widget = {"card", "card", {0}};
    anchor.key.subject = parameter;
    anchor.targetRect = rect;
    anchor.anchorRect = rect;
    anchor.anchorHitRect = rect;
    anchor.anchorHitPadding = 2;
    anchor.draggable = true;
    return anchor;
}

}  // namespace

TEST(LayoutEditHitTest, AnchorHandleBeatsOverlappingGapAndWidgetGuideByPriority) {
    LayoutEditAnchorRegion anchor = BasicAnchor(LayoutEditParameter::FontLabel, RenderRect{10, 10, 20, 20});
    anchor.shape = AnchorShape::Square;

    LayoutEditGapAnchor gap;
    gap.key.widget = {"card", "card", {}};
    gap.key.parameter = LayoutEditParameter::MetricListRowGap;
    gap.hitRect = RenderRect{0, 0, 30, 30};

    LayoutEditWidgetGuide guide;
    guide.widget = {"card", "card", {0}};
    guide.parameter = LayoutEditParameter::MetricListRowGap;
    guide.hitRect = RenderRect{0, 0, 30, 30};

    const std::vector<LayoutEditActiveRegion> regions{
        GapRegion(gap), WidgetGuideRegion(guide), AnchorHandleRegion(anchor)};

    const LayoutEditHoverResolution hover = ResolveLayoutEditHover(regions, RenderPoint{15, 15});

    ASSERT_TRUE(hover.actionableAnchorHandle.has_value());
    EXPECT_EQ(std::get<LayoutEditParameter>(hover.actionableAnchorHandle->subject), LayoutEditParameter::FontLabel);
    EXPECT_FALSE(hover.actionableGapEditAnchor.has_value());
    EXPECT_FALSE(hover.hoveredWidgetEditGuide.has_value());
}

TEST(LayoutEditHitTest, CircleHandleHitsRingNotCenter) {
    LayoutEditAnchorRegion anchor = BasicAnchor(LayoutEditParameter::FontLabel, RenderRect{10, 10, 20, 20});
    anchor.shape = AnchorShape::Circle;
    anchor.anchorHitPadding = 1;

    const std::vector<LayoutEditActiveRegion> regions{AnchorHandleRegion(anchor)};

    EXPECT_TRUE(HitTestEditableAnchorHandle(regions, RenderPoint{20, 15}).has_value());
    EXPECT_FALSE(HitTestEditableAnchorHandle(regions, RenderPoint{15, 15}).has_value());
}

TEST(LayoutEditHitTest, AnchorTargetUsesSmallestContainingArea) {
    LayoutEditAnchorRegion large = BasicAnchor(LayoutEditParameter::CardPadding, RenderRect{0, 0, 100, 100});
    LayoutEditAnchorRegion small = BasicAnchor(LayoutEditParameter::FontLabel, RenderRect{20, 20, 30, 30});

    const std::vector<LayoutEditActiveRegion> regions{AnchorTargetRegion(large), AnchorTargetRegion(small)};

    const auto hit = HitTestEditableAnchorTarget(regions, RenderPoint{25, 25});

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(std::get<LayoutEditParameter>(hit->key.subject), LayoutEditParameter::FontLabel);
}

TEST(LayoutEditHitTest, ColorRegionUsesPriorityThenSmallestArea) {
    LayoutEditColorRegion lowPriorityLarge{LayoutEditParameter::ColorAccent, RenderRect{0, 0, 100, 100}};
    LayoutEditColorRegion highPrioritySmall{LayoutEditParameter::ColorForeground, RenderRect{20, 20, 40, 40}};

    const std::vector<LayoutEditActiveRegion> regions{ColorRegion(lowPriorityLarge), ColorRegion(highPrioritySmall)};

    const auto hit = HitTestEditableColorRegion(regions, RenderPoint{25, 25});

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->parameter, LayoutEditParameter::ColorForeground);
}

TEST(LayoutEditHitTest, FindsGuideFamiliesByIdentity) {
    LayoutEditGuide layoutGuide;
    layoutGuide.editCardId = "card";
    layoutGuide.nodePath = {1, 2};
    layoutGuide.separatorIndex = 3;
    layoutGuide.hitRect = RenderRect{1, 1, 5, 5};

    LayoutEditWidgetGuide widgetGuide;
    widgetGuide.widget = {"card", "card", {0}};
    widgetGuide.parameter = LayoutEditParameter::MetricListRowGap;
    widgetGuide.guideId = 7;
    widgetGuide.hitRect = RenderRect{10, 10, 20, 20};

    LayoutEditGapAnchor gap;
    gap.key.widget = {"card", "card", {}};
    gap.key.parameter = LayoutEditParameter::CardColumnGap;
    gap.hitRect = RenderRect{30, 30, 40, 40};

    const std::vector<LayoutEditActiveRegion> regions{
        LayoutGuideRegion(layoutGuide), WidgetGuideRegion(widgetGuide), GapRegion(gap)};

    EXPECT_TRUE(FindLayoutEditGuide(regions, layoutGuide).has_value());
    EXPECT_TRUE(FindWidgetEditGuide(regions, widgetGuide).has_value());
    EXPECT_TRUE(FindGapEditAnchor(regions, gap.key).has_value());
}
