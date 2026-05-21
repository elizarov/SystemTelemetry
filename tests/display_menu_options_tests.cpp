#include <windows.h>

#include <gtest/gtest.h>
#include <optional>

#include "config/config.h"
#include "display/constants.h"
#include "display/monitor.h"
#include "util/scale.h"

namespace {

AppConfig MakeDisplayConfig(int layoutWidth, int layoutHeight) {
    AppConfig config;
    config.layout.structure.window.width = layoutWidth;
    config.layout.structure.window.height = layoutHeight;
    return config;
}

RECT MakeRect(int width, int height) {
    return RECT{0, 0, width, height};
}

DisplayMenuMonitorInfo MakeMonitor(int width, int height) {
    return DisplayMenuMonitorInfo{"Panel", "Panel", MakeRect(width, height), USER_DEFAULT_SCREEN_DPI};
}

TargetMonitorInfo MakeTargetMonitor(int width, int height) {
    return TargetMonitorInfo{MakeRect(width, height), USER_DEFAULT_SCREEN_DPI};
}

}  // namespace

TEST(DisplayMenuOptions, MatchingAspectRatioYieldsOneFullscreenOption) {
    const AppConfig config = MakeDisplayConfig(1600, 900);
    DisplayMenuOption options[3]{};

    const size_t count =
        BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1920, 1080), std::nullopt, false, options, 3);

    ASSERT_EQ(count, 1u);
    EXPECT_EQ(options[0].label, "Panel 1920x1080 full screen");
    EXPECT_EQ(options[0].placementMode, DisplayPlacementMode::FullScreen);
    EXPECT_EQ(options[0].targetSize.cx, 1920);
    EXPECT_EQ(options[0].targetSize.cy, 1080);
    EXPECT_EQ(options[0].position, LogicalPointConfig{});
    EXPECT_NEAR(options[0].targetScale, 1.2, 0.000001);
    EXPECT_TRUE(options[0].writesWallpaper);
}

TEST(DisplayMenuOptions, WiderLayoutYieldsTopAndBottomOptions) {
    const AppConfig config = MakeDisplayConfig(1600, 900);
    DisplayMenuOption options[3]{};

    const size_t count =
        BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1200, 1000), std::nullopt, false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_EQ(options[0].label, "Panel 1200x675 top");
    EXPECT_EQ(options[0].placementMode, DisplayPlacementMode::Top);
    EXPECT_EQ(options[0].targetSize.cx, 1200);
    EXPECT_EQ(options[0].targetSize.cy, 675);
    EXPECT_EQ(options[0].position, LogicalPointConfig{});
    EXPECT_NEAR(options[0].targetScale, 0.75, 0.000001);
    EXPECT_FALSE(options[0].writesWallpaper);
    EXPECT_EQ(options[1].label, "Panel 1200x675 bottom");
    EXPECT_EQ(options[1].placementMode, DisplayPlacementMode::Bottom);
    EXPECT_EQ(options[1].position, (LogicalPointConfig{0, ScalePhysicalToLogical(325, 0.75)}));
    EXPECT_NEAR(options[1].targetScale, 0.75, 0.000001);
    EXPECT_FALSE(options[1].writesWallpaper);
}

TEST(DisplayMenuOptions, EdgeScaleIsRoundedToThreeDecimals) {
    const AppConfig config = MakeDisplayConfig(3000, 1000);
    DisplayMenuOption options[3]{};

    const size_t count =
        BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1000, 600), std::nullopt, false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_EQ(options[0].label, "Panel 999x333 top");
    EXPECT_EQ(options[0].targetSize.cx, 999);
    EXPECT_EQ(options[0].targetSize.cy, 333);
    EXPECT_NEAR(options[0].targetScale, 0.333, 0.000001);
    EXPECT_EQ(options[1].label, "Panel 999x333 bottom");
}

TEST(DisplayMenuOptions, NarrowerLayoutYieldsLeftAndRightOptions) {
    const AppConfig config = MakeDisplayConfig(900, 1600);
    DisplayMenuOption options[3]{};

    const size_t count =
        BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1200, 1000), std::nullopt, false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_EQ(options[0].label, "Panel 563x1000 left");
    EXPECT_EQ(options[0].placementMode, DisplayPlacementMode::Left);
    EXPECT_EQ(options[0].targetSize.cx, 563);
    EXPECT_EQ(options[0].targetSize.cy, 1000);
    EXPECT_EQ(options[0].position, LogicalPointConfig{});
    EXPECT_NEAR(options[0].targetScale, 0.625, 0.000001);
    EXPECT_FALSE(options[0].writesWallpaper);
    EXPECT_EQ(options[1].label, "Panel 563x1000 right");
    EXPECT_EQ(options[1].placementMode, DisplayPlacementMode::Right);
    EXPECT_EQ(options[1].position, (LogicalPointConfig{ScalePhysicalToLogical(637, 0.625), 0}));
    EXPECT_NEAR(options[1].targetScale, 0.625, 0.000001);
    EXPECT_FALSE(options[1].writesWallpaper);
}

TEST(DisplayMenuOptions, CurrentConfigMatchesMonitorScaleAndPosition) {
    AppConfig config = MakeDisplayConfig(1600, 900);
    config.display.scale = 0.75;
    config.display.position = LogicalPointConfig{0, ScalePhysicalToLogical(325, 0.75)};
    DisplayMenuOption options[3]{};

    const size_t count = BuildDisplayMenuOptionsForMonitor(
        config, MakeMonitor(1200, 1000), MakeTargetMonitor(1200, 1000), false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_FALSE(options[0].matchesCurrentConfig);
    EXPECT_TRUE(options[1].matchesCurrentConfig);
}

TEST(DisplayMenuOptions, GeneratedOptionsHaveActionableTargets) {
    const AppConfig config = MakeDisplayConfig(1600, 900);
    DisplayMenuOption options[3]{};

    const size_t count =
        BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1200, 1000), std::nullopt, true, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_TRUE(options[0].startsSection);
    for (size_t i = 0; i < count; ++i) {
        EXPECT_FALSE(options[i].label.empty());
        EXPECT_FALSE(options[i].configMonitorName.empty());
        EXPECT_GT(options[i].targetScale, 0.0);
        EXPECT_GT(options[i].targetSize.cx, 0);
        EXPECT_GT(options[i].targetSize.cy, 0);
    }
}

TEST(DisplayConfiguration, FullscreenConfigWritesWallpaper) {
    const AppConfig config = MakeDisplayConfig(1600, 900);
    DisplayMenuOption options[1]{};
    ASSERT_EQ(BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1920, 1080), std::nullopt, false, options, 1), 1u);

    const AppConfig updated = BuildConfiguredDisplayConfig(config, options[0]);

    EXPECT_EQ(updated.display.monitorName, "Panel");
    EXPECT_EQ(updated.display.position, LogicalPointConfig{});
    EXPECT_NEAR(updated.display.scale, 1.2, 0.000001);
    EXPECT_EQ(updated.display.wallpaper, kDefaultBlankWallpaperFileName);
}

TEST(DisplayConfiguration, EdgeConfigClearsWallpaperValue) {
    const AppConfig config = MakeDisplayConfig(1600, 900);
    DisplayMenuOption options[2]{};
    ASSERT_EQ(BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1200, 1000), std::nullopt, false, options, 2), 2u);

    const AppConfig updated = BuildConfiguredDisplayConfig(config, options[1]);

    EXPECT_EQ(updated.display.monitorName, "Panel");
    EXPECT_EQ(updated.display.position, (LogicalPointConfig{0, ScalePhysicalToLogical(325, 0.75)}));
    EXPECT_NEAR(updated.display.scale, 0.75, 0.000001);
    EXPECT_TRUE(updated.display.wallpaper.empty());
}

TEST(DisplayConfiguration, PreviousWallpaperClearRequestsFollowPlacementAndMonitor) {
    AppConfig previous = MakeDisplayConfig(1600, 900);
    previous.display.wallpaper = kDefaultBlankWallpaperFileName;

    DisplayMenuOption fullscreenOption;
    fullscreenOption.writesWallpaper = true;
    fullscreenOption.monitorRect = MakeRect(1920, 1080);
    EXPECT_FALSE(ShouldClearPreviousDisplayWallpaper(previous, MakeTargetMonitor(1920, 1080), fullscreenOption));
    EXPECT_TRUE(ShouldClearPreviousDisplayWallpaper(previous, MakeTargetMonitor(1200, 1000), fullscreenOption));

    DisplayMenuOption edgeOption = fullscreenOption;
    edgeOption.writesWallpaper = false;
    EXPECT_TRUE(ShouldClearPreviousDisplayWallpaper(previous, MakeTargetMonitor(1920, 1080), edgeOption));

    previous.display.wallpaper.clear();
    EXPECT_FALSE(ShouldClearPreviousDisplayWallpaper(previous, MakeTargetMonitor(1200, 1000), fullscreenOption));
}
