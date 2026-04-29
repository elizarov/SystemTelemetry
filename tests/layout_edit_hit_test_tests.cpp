#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "config/config_parser.h"
#include "config/config_resolution.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "dashboard_renderer/impl/layout_edit_overlay_renderer.h"
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

bool RectsOverlap(const RenderRect& lhs, const RenderRect& rhs) {
    return lhs.left < rhs.right && lhs.right > rhs.left && lhs.top < rhs.bottom && lhs.bottom > rhs.top;
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
            return target.childRect.left == region.box.left && target.childRect.top == region.box.top &&
                   target.childRect.right == region.box.right && target.childRect.bottom == region.box.bottom &&
                   target.childRect.Contains(point);
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
            const auto hit = HitTestEditableAnchorHandle(regions, point);
            return hit.has_value() &&
                   MatchesEditableAnchorKey(hit->key, std::get<LayoutEditAnchorRegion>(region.payload).key);
        }
        case LayoutEditActiveRegionKind::StaticEditAnchorTarget:
        case LayoutEditActiveRegionKind::DynamicEditAnchorTarget: {
            const auto hit = HitTestEditableAnchorTarget(regions, point);
            return hit.has_value();
        }
        case LayoutEditActiveRegionKind::StaticColorTarget:
        case LayoutEditActiveRegionKind::DynamicColorTarget: {
            const auto hit = HitTestEditableColorRegion(regions, point);
            return hit.has_value();
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

// This test helper proves effective hover reachability, not just geometric overlap: every point in the 4x4 block must
// resolve through the same hit-test path used by the UI. Independent handles must resolve exactly; target highlights
// may resolve to another anchor target because the UI opens one action per hover point while sharing component outlines.
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

TEST(LayoutEditHitTest, TextFormatWedgeHoverKeepsRelatedTextTargetOutline) {
    LayoutEditAnchorRegion fontAnchor = BasicAnchor(LayoutEditParameter::FontClockDate, RenderRect{20, 30, 120, 50});
    fontAnchor.key.anchorId = 0;
    fontAnchor.shape = AnchorShape::Circle;
    fontAnchor.drawTargetOutline = true;

    LayoutEditAnchorRegion formatWedge = fontAnchor;
    formatWedge.key.subject = LayoutNodeFieldEditKey{"time", {1}, WidgetClass::ClockDate, LayoutNodeField::Parameter};
    formatWedge.key.anchorId = 1;
    formatWedge.shape = AnchorShape::Wedge;
    formatWedge.draggable = false;
    formatWedge.drawTargetOutline = false;

    const std::vector<LayoutEditAnchorRegion> highlights =
        CollectRelatedEditableAnchorHighlights({}, {fontAnchor, formatWedge}, formatWedge);

    const auto relatedFont = std::find_if(highlights.begin(), highlights.end(), [&](const auto& region) {
        return MatchesEditableAnchorKey(region.key, fontAnchor.key);
    });
    ASSERT_NE(relatedFont, highlights.end());
    EXPECT_TRUE(relatedFont->drawTargetOutline);
}

TEST(LayoutEditHitTest, CpuMetricListClockRowAndContainerReorderAnchorsDoNotOverlap) {
    AppConfig config = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());
    ASSERT_TRUE(SelectLayout(config, "5x3"));

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
    std::optional<LayoutEditAnchorRegion> clockRowAnchor;
    std::vector<LayoutEditAnchorRegion> containerAnchors;
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::StaticEditAnchorHandle) {
            continue;
        }
        const auto& anchor = std::get<LayoutEditAnchorRegion>(region.payload);
        if (anchor.key.widget.editCardId != "cpu") {
            continue;
        }
        if (const auto* nodeField = std::get_if<LayoutNodeFieldEditKey>(&anchor.key.subject);
            nodeField != nullptr && nodeField->widgetClass == WidgetClass::MetricList && anchor.key.anchorId == 2) {
            clockRowAnchor = anchor;
        }
        if (std::holds_alternative<LayoutContainerChildOrderEditKey>(anchor.key.subject)) {
            containerAnchors.push_back(anchor);
        }
    }

    ASSERT_TRUE(clockRowAnchor.has_value());
    const auto containerAnchor = std::find_if(containerAnchors.begin(),
        containerAnchors.end(),
        [&](const auto& anchor) { return anchor.targetRect.Contains(clockRowAnchor->anchorRect.Center()); });
    ASSERT_NE(containerAnchor, containerAnchors.end());

    EXPECT_FALSE(RectsOverlap(clockRowAnchor->anchorRect, containerAnchor->anchorRect));
    EXPECT_FALSE(RectsOverlap(clockRowAnchor->anchorHitRect, containerAnchor->anchorHitRect));
    EXPECT_TRUE(HasFourByFourHitBlock(regions, AnchorHandleRegion(*clockRowAnchor)));
    EXPECT_TRUE(HasFourByFourHitBlock(regions, AnchorHandleRegion(*containerAnchor)));
}

TEST(LayoutEditHitTest, BuiltInLayoutsActiveRegionsHaveFourByFourReachableHitZone) {
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

    ASSERT_FALSE(config.layout.layouts.empty());
    for (const LayoutSectionConfig& layout : config.layout.layouts) {
        SCOPED_TRACE(layout.name);
        ASSERT_TRUE(SelectLayout(config, layout.name));
        renderer.SetConfig(config);
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
}
