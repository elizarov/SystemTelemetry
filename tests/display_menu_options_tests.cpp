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

DisplayMenuOption MakeSchematicOption(
    DisplayPlacementMode mode, int monitorWidth, int monitorHeight, int targetWidth, int targetHeight) {
    DisplayMenuOption option;
    option.monitorRect = MakeRect(monitorWidth, monitorHeight);
    option.targetSize = SIZE{targetWidth, targetHeight};
    option.placementMode = mode;
    return option;
}

void ExpectRect(const RECT& rect, LONG left, LONG top, LONG right, LONG bottom) {
    EXPECT_EQ(rect.left, left);
    EXPECT_EQ(rect.top, top);
    EXPECT_EQ(rect.right, right);
    EXPECT_EQ(rect.bottom, bottom);
}

}  // namespace

TEST(DisplayMenuOptions, MatchingAspectRatioYieldsOneFullscreenOption) {
    const AppConfig config = MakeDisplayConfig(1600, 900);
    DisplayMenuOption options[3]{};

    const size_t count =
        BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1920, 1080), std::nullopt, false, options, 3);

    ASSERT_EQ(count, 1u);
    EXPECT_EQ(options[0].label, "Panel full screen");
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
    EXPECT_EQ(options[0].label, "Panel top");
    EXPECT_EQ(options[0].placementMode, DisplayPlacementMode::Top);
    EXPECT_EQ(options[0].targetSize.cx, 1200);
    EXPECT_EQ(options[0].targetSize.cy, 675);
    EXPECT_EQ(options[0].position, LogicalPointConfig{});
    EXPECT_NEAR(options[0].targetScale, 0.75, 0.000001);
    EXPECT_FALSE(options[0].writesWallpaper);
    EXPECT_EQ(options[1].label, "Panel bottom");
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
    EXPECT_EQ(options[0].label, "Panel top");
    EXPECT_EQ(options[0].targetSize.cx, 999);
    EXPECT_EQ(options[0].targetSize.cy, 333);
    EXPECT_NEAR(options[0].targetScale, 0.333, 0.000001);
    EXPECT_EQ(options[1].label, "Panel bottom");
}

TEST(DisplayMenuOptions, NarrowerLayoutYieldsLeftAndRightOptions) {
    const AppConfig config = MakeDisplayConfig(900, 1600);
    DisplayMenuOption options[3]{};

    const size_t count =
        BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1200, 1000), std::nullopt, false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_EQ(options[0].label, "Panel left");
    EXPECT_EQ(options[0].placementMode, DisplayPlacementMode::Left);
    EXPECT_EQ(options[0].targetSize.cx, 563);
    EXPECT_EQ(options[0].targetSize.cy, 1000);
    EXPECT_EQ(options[0].position, LogicalPointConfig{});
    EXPECT_NEAR(options[0].targetScale, 0.625, 0.000001);
    EXPECT_FALSE(options[0].writesWallpaper);
    EXPECT_EQ(options[1].label, "Panel right");
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

TEST(DisplayPlacementSchematic, PreservesDisplayAspectRatioInsideBounds) {
    const DisplayMenuOption option = MakeSchematicOption(DisplayPlacementMode::FullScreen, 1600, 900, 1600, 900);

    const DisplayPlacementSchematicGeometry geometry =
        ComputeDisplayPlacementSchematicGeometry(option, RECT{0, 0, 32, 32});

    ExpectRect(geometry.displayRect, 0, 7, 32, 25);
    ExpectRect(geometry.caseDashRect, 0, 7, 32, 25);
    EXPECT_FALSE(geometry.hasDivider);
}

TEST(DisplayPlacementSchematic, FullscreenFillsDisplayRect) {
    const DisplayMenuOption option = MakeSchematicOption(DisplayPlacementMode::FullScreen, 1200, 1000, 1200, 1000);

    const DisplayPlacementSchematicGeometry geometry =
        ComputeDisplayPlacementSchematicGeometry(option, RECT{0, 0, 120, 100});

    ExpectRect(geometry.displayRect, 0, 0, 120, 100);
    ExpectRect(geometry.caseDashRect, 0, 0, 120, 100);
    EXPECT_FALSE(geometry.hasDivider);
}

TEST(DisplayPlacementSchematic, TopAndBottomUseFittedLayoutHeightRatio) {
    const DisplayMenuOption top = MakeSchematicOption(DisplayPlacementMode::Top, 1200, 1000, 1200, 675);
    const DisplayMenuOption bottom = MakeSchematicOption(DisplayPlacementMode::Bottom, 1200, 1000, 1200, 675);

    const DisplayPlacementSchematicGeometry topGeometry =
        ComputeDisplayPlacementSchematicGeometry(top, RECT{0, 0, 120, 100});
    const DisplayPlacementSchematicGeometry bottomGeometry =
        ComputeDisplayPlacementSchematicGeometry(bottom, RECT{0, 0, 120, 100});

    ExpectRect(topGeometry.caseDashRect, 0, 0, 120, 68);
    EXPECT_TRUE(topGeometry.hasDivider);
    ExpectRect(topGeometry.dividerRect, 0, 68, 120, 69);
    ExpectRect(bottomGeometry.caseDashRect, 0, 32, 120, 100);
    EXPECT_TRUE(bottomGeometry.hasDivider);
    ExpectRect(bottomGeometry.dividerRect, 0, 32, 120, 33);
}

TEST(DisplayPlacementSchematic, LeftAndRightUseFittedLayoutWidthRatio) {
    const DisplayMenuOption left = MakeSchematicOption(DisplayPlacementMode::Left, 1200, 1000, 563, 1000);
    const DisplayMenuOption right = MakeSchematicOption(DisplayPlacementMode::Right, 1200, 1000, 563, 1000);

    const DisplayPlacementSchematicGeometry leftGeometry =
        ComputeDisplayPlacementSchematicGeometry(left, RECT{0, 0, 120, 100});
    const DisplayPlacementSchematicGeometry rightGeometry =
        ComputeDisplayPlacementSchematicGeometry(right, RECT{0, 0, 120, 100});

    ExpectRect(leftGeometry.caseDashRect, 0, 0, 56, 100);
    EXPECT_TRUE(leftGeometry.hasDivider);
    ExpectRect(leftGeometry.dividerRect, 56, 0, 57, 100);
    ExpectRect(rightGeometry.caseDashRect, 64, 0, 120, 100);
    EXPECT_TRUE(rightGeometry.hasDivider);
    ExpectRect(rightGeometry.dividerRect, 64, 0, 65, 100);
}

TEST(DisplayAspectResize, DiagonalExtentReturnsRoundedScale) {
    EXPECT_NEAR(ComputeAspectResizeScale(SIZE{1600, 900}, POINT{1600, 900}), 1.0, 0.000001);
    EXPECT_NEAR(ComputeAspectResizeScale(SIZE{3000, 1000}, POINT{999, 333}), 0.333, 0.000001);
}

TEST(DisplayAspectResize, HorizontalAndVerticalDragsPreserveAspectByProjection) {
    EXPECT_NEAR(ComputeAspectResizeScale(SIZE{1600, 900}, POINT{2000, 900}), 1.190, 0.000001);
    EXPECT_NEAR(ComputeAspectResizeScale(SIZE{1600, 900}, POINT{1600, 1200}), 1.080, 0.000001);
}

TEST(DisplayAspectResize, ClampsToSupportedScaleRange) {
    EXPECT_NEAR(ComputeAspectResizeScale(SIZE{1600, 900}, POINT{100000, 100000}), 16.0, 0.000001);
    EXPECT_NEAR(ComputeAspectResizeScale(SIZE{1600, 900}, POINT{-100, -100}), 0.1, 0.000001);
}

TEST(DisplayResizeConfiguration, UpdatesPlacementAndPreservesWallpaper) {
    DisplayConfig display;
    display.monitorName = "Old";
    display.wallpaper = "keep.png";
    display.position = LogicalPointConfig{2, 3};
    display.scale = 1.0;
    MonitorPlacementInfo placement;
    placement.deviceName = "DISPLAY1";
    placement.configMonitorName = "Panel";
    placement.relativePosition = POINT{10, 20};

    const DisplayConfig updated = BuildResizePlacementDisplayConfig(display, placement, 1.23456);

    EXPECT_EQ(updated.monitorName, "Panel");
    EXPECT_EQ(updated.position, (LogicalPointConfig{10, 20}));
    EXPECT_NEAR(updated.scale, 1.235, 0.000001);
    EXPECT_EQ(updated.wallpaper, "keep.png");
}

TEST(DisplayResizeConfiguration, FallsBackToDeviceNameAndDetectsNoOp) {
    DisplayConfig display;
    display.monitorName = "DISPLAY1";
    display.wallpaper = "keep.png";
    display.position = LogicalPointConfig{10, 20};
    display.scale = 1.235;
    MonitorPlacementInfo placement;
    placement.deviceName = "DISPLAY1";
    placement.relativePosition = POINT{10, 20};

    const DisplayConfig updated = BuildResizePlacementDisplayConfig(display, placement, 1.235);

    EXPECT_EQ(updated, display);
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
