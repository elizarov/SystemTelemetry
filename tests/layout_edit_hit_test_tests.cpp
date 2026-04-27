#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "config/config_parser.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_edit/layout_edit_hit_test.h"
#include "layout_model/dashboard_overlay_state.h"
#include "layout_model/layout_edit_helpers.h"
#include "telemetry/impl/collector_fake.h"
#include "util/trace.h"

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

std::filesystem::path SourceConfigPath() {
    return std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
}

ConfigParseContext TestConfigParseContext() {
    return ConfigParseContext{TelemetryMetricCatalog()};
}

bool IsAnchorHandleKind(LayoutEditActiveRegionKind kind) {
    return kind == LayoutEditActiveRegionKind::StaticEditAnchorHandle ||
           kind == LayoutEditActiveRegionKind::DynamicEditAnchorHandle;
}

bool IsAnchorTargetKind(LayoutEditActiveRegionKind kind) {
    return kind == LayoutEditActiveRegionKind::StaticEditAnchorTarget ||
           kind == LayoutEditActiveRegionKind::DynamicEditAnchorTarget;
}

bool IsColorTargetKind(LayoutEditActiveRegionKind kind) {
    return kind == LayoutEditActiveRegionKind::StaticColorTarget ||
           kind == LayoutEditActiveRegionKind::DynamicColorTarget;
}

bool MatchesCardRegion(const LayoutEditHoverResolution& hover, const LayoutEditCardRegion& card) {
    return hover.hoveredLayoutCard.has_value() &&
           hover.hoveredLayoutCard->kind == LayoutEditWidgetIdentity::Kind::CardChrome &&
           hover.hoveredLayoutCard->editCardId == card.id;
}

bool MatchesCardHeaderRegion(const LayoutEditHoverResolution& hover, const LayoutEditCardRegion& card) {
    return hover.hoveredEditableCard.has_value() &&
           hover.hoveredEditableCard->kind == LayoutEditWidgetIdentity::Kind::CardChrome &&
           hover.hoveredEditableCard->editCardId == card.id;
}

bool MatchesRegionHit(const LayoutEditActiveRegions& regions, const LayoutEditActiveRegion& region, RenderPoint point) {
    const LayoutEditHoverResolution hover = ResolveLayoutEditHover(regions, point);

    switch (region.kind) {
        case LayoutEditActiveRegionKind::Card:
            return MatchesCardRegion(hover, std::get<LayoutEditCardRegion>(region.payload));
        case LayoutEditActiveRegionKind::CardHeader:
            return MatchesCardHeaderRegion(hover, std::get<LayoutEditCardRegion>(region.payload));
        case LayoutEditActiveRegionKind::WidgetHover:
            return hover.hoveredEditableWidget.has_value() &&
                   MatchesWidgetIdentity(
                       *hover.hoveredEditableWidget, std::get<LayoutEditWidgetRegion>(region.payload).widget);
        case LayoutEditActiveRegionKind::LayoutWeightGuide:
            if (const auto guide = HitTestLayoutGuide(regions, point); guide.has_value()) {
                return MatchesLayoutEditGuide(*guide, std::get<LayoutEditGuide>(region.payload));
            }
            return false;
        case LayoutEditActiveRegionKind::ContainerChildReorderTarget: {
            const auto& target = std::get<LayoutEditContainerChildReorderRegion>(region.payload);
            for (const RenderRect& childRect : target.childRects) {
                if (childRect.left == region.box.left && childRect.top == region.box.top &&
                    childRect.right == region.box.right && childRect.bottom == region.box.bottom &&
                    childRect.Contains(point)) {
                    return true;
                }
            }
            return false;
        }
        case LayoutEditActiveRegionKind::GapHandle:
            if (const auto gap = HitTestGapEditAnchor(regions, point); gap.has_value()) {
                return MatchesGapEditAnchorKey(gap->key, std::get<LayoutEditGapAnchor>(region.payload).key);
            }
            return false;
        case LayoutEditActiveRegionKind::WidgetGuide:
            return std::get<LayoutEditWidgetGuide>(region.payload).hitRect.Contains(point);
        case LayoutEditActiveRegionKind::StaticEditAnchorHandle:
        case LayoutEditActiveRegionKind::DynamicEditAnchorHandle: {
            return std::get<LayoutEditAnchorRegion>(region.payload).anchorHitRect.Contains(point);
        }
        case LayoutEditActiveRegionKind::StaticEditAnchorTarget:
        case LayoutEditActiveRegionKind::DynamicEditAnchorTarget: {
            return std::get<LayoutEditAnchorRegion>(region.payload).targetRect.Contains(point);
        }
        case LayoutEditActiveRegionKind::StaticColorTarget:
        case LayoutEditActiveRegionKind::DynamicColorTarget: {
            return std::get<LayoutEditColorRegion>(region.payload).targetRect.Contains(point);
        }
    }
    return false;
}

std::string RegionLabel(const LayoutEditActiveRegion& region) {
    std::ostringstream stream;
    stream << "kind=" << static_cast<int>(region.kind) << " box=(" << region.box.left << "," << region.box.top << ","
           << region.box.right << "," << region.box.bottom << ")";
    if (IsAnchorHandleKind(region.kind) || IsAnchorTargetKind(region.kind)) {
        const auto& anchor = std::get<LayoutEditAnchorRegion>(region.payload);
        stream << " anchor_widget=" << anchor.key.widget.editCardId << " anchor_id=" << anchor.key.anchorId;
    }
    if (IsColorTargetKind(region.kind)) {
        stream << " color_parameter=" << static_cast<int>(std::get<LayoutEditColorRegion>(region.payload).parameter);
    }
    return stream.str();
}

std::vector<int> CandidateStarts(int begin, int end) {
    std::vector<int> starts;
    if (end - begin < 4) {
        return starts;
    }
    const int last = end - 4;
    starts.push_back(begin);
    for (int value = begin + 4; value <= last; value += 4) {
        starts.push_back(value);
    }
    starts.push_back(last);
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    return starts;
}

bool HasFourByFourHitBlock(const LayoutEditActiveRegions& regions, const LayoutEditActiveRegion& region) {
    const auto cornersHit = [&](int x, int y) {
        return MatchesRegionHit(regions, region, RenderPoint{x, y}) &&
               MatchesRegionHit(regions, region, RenderPoint{x + 3, y}) &&
               MatchesRegionHit(regions, region, RenderPoint{x, y + 3}) &&
               MatchesRegionHit(regions, region, RenderPoint{x + 3, y + 3});
    };

    for (int y : CandidateStarts(region.box.top, region.box.bottom)) {
        for (int x : CandidateStarts(region.box.left, region.box.right)) {
            if (cornersHit(x, y)) {
                return true;
            }
        }
    }

    for (int y = region.box.top; y <= region.box.bottom - 4; ++y) {
        for (int x = region.box.left; x <= region.box.right - 4; ++x) {
            if (cornersHit(x, y)) {
                return true;
            }
        }
    }
    return false;
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

    const LayoutEditActiveRegions regions{GapRegion(gap), WidgetGuideRegion(guide), AnchorHandleRegion(anchor)};

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

    const LayoutEditActiveRegions regions{AnchorHandleRegion(anchor)};

    EXPECT_TRUE(HitTestEditableAnchorHandle(regions, RenderPoint{20, 15}).has_value());
    EXPECT_FALSE(HitTestEditableAnchorHandle(regions, RenderPoint{15, 15}).has_value());
}

TEST(LayoutEditHitTest, AnchorTargetUsesSmallestContainingArea) {
    LayoutEditAnchorRegion large = BasicAnchor(LayoutEditParameter::CardPadding, RenderRect{0, 0, 100, 100});
    LayoutEditAnchorRegion small = BasicAnchor(LayoutEditParameter::FontLabel, RenderRect{20, 20, 30, 30});

    const LayoutEditActiveRegions regions{AnchorTargetRegion(large), AnchorTargetRegion(small)};

    const auto hit = HitTestEditableAnchorTarget(regions, RenderPoint{25, 25});

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(std::get<LayoutEditParameter>(hit->key.subject), LayoutEditParameter::FontLabel);
}

TEST(LayoutEditHitTest, ColorRegionUsesPriorityThenSmallestArea) {
    LayoutEditColorRegion lowPriorityLarge{LayoutEditParameter::ColorAccent, RenderRect{0, 0, 100, 100}};
    LayoutEditColorRegion highPrioritySmall{LayoutEditParameter::ColorForeground, RenderRect{20, 20, 40, 40}};

    const LayoutEditActiveRegions regions{ColorRegion(lowPriorityLarge), ColorRegion(highPrioritySmall)};

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

    const LayoutEditActiveRegions regions{
        LayoutGuideRegion(layoutGuide), WidgetGuideRegion(widgetGuide), GapRegion(gap)};

    EXPECT_TRUE(FindLayoutEditGuide(regions, layoutGuide).has_value());
    EXPECT_TRUE(FindWidgetEditGuide(regions, widgetGuide).has_value());
    EXPECT_TRUE(FindGapEditAnchor(regions, gap.key).has_value());
}

TEST(LayoutEditHitTest, BuiltInDefaultLayoutActiveRegionsHaveFourByFourReachableHitZone) {
    AppConfig config = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());

    Trace trace;
    std::unique_ptr<TelemetryCollector> telemetry =
        CreateFakeTelemetryCollector(std::filesystem::current_path(), {}, nullptr, trace);
    ASSERT_NE(telemetry, nullptr);
    std::string telemetryError;
    ASSERT_TRUE(telemetry->Initialize(ExtractTelemetrySettings(config), &telemetryError)) << telemetryError;
    telemetry->UpdateSnapshot();

    DashboardRenderer renderer(trace);
    renderer.SetConfig(config);

    DashboardOverlayState overlayState;
    overlayState.showLayoutEditGuides = true;

    ASSERT_TRUE(renderer.RenderSnapshotOffscreen(telemetry->Snapshot(), overlayState)) << renderer.LastError();

    const LayoutEditActiveRegions regions = renderer.CollectLayoutEditActiveRegions(overlayState);
    ASSERT_FALSE(regions.Empty());

    size_t index = 0;
    for (const LayoutEditActiveRegion& region : regions) {
        SCOPED_TRACE(index);
        SCOPED_TRACE(RegionLabel(region));
        EXPECT_GE(region.box.Width(), 4);
        EXPECT_GE(region.box.Height(), 4);
        EXPECT_TRUE(HasFourByFourHitBlock(regions, region));
        ++index;
    }
}
