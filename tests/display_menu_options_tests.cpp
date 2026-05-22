#include <windows.h>

#include <algorithm>
#include <gtest/gtest.h>
#include <optional>
#include <vector>

#include "config/config.h"
#include "dashboard/display_placement_menu_bitmap.h"
#include "dashboard/native_theme_colors.h"
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

RECT MakeRectAt(int left, int top, int width, int height) {
    return RECT{left, top, left + width, top + height};
}

DisplayMenuMonitorInfo MakeMonitor(int width, int height) {
    return DisplayMenuMonitorInfo{"Panel", "Panel", MakeRect(width, height), USER_DEFAULT_SCREEN_DPI};
}

TargetMonitorInfo MakeTargetMonitor(int width, int height) {
    return TargetMonitorInfo{MakeRect(width, height), USER_DEFAULT_SCREEN_DPI};
}

TargetMonitorInfo MakeTargetMonitorAt(int left, int top, int width, int height) {
    return TargetMonitorInfo{MakeRectAt(left, top, width, height), USER_DEFAULT_SCREEN_DPI};
}

AppConfig MakeFullscreenWallpaperConfig(
    const std::string& monitorName, int layoutWidth, int layoutHeight, const TargetMonitorInfo& monitor) {
    AppConfig config = MakeDisplayConfig(layoutWidth, layoutHeight);
    config.display.monitorName = monitorName;
    config.display.wallpaper = kDefaultBlankWallpaperFileName;
    config.display.position = LogicalPointConfig{};
    config.display.scale = ComputeMonitorFittedScale(
        config, monitor.rect.right - monitor.rect.left, monitor.rect.bottom - monitor.rect.top);
    return config;
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

std::vector<DisplayPlacementMenuBitmapPixel> PaintMenuBitmapForTest(DisplayMenuOption option) {
    constexpr int kBitmapSize = 32;
    constexpr COLORREF kMenuColor = RGB(11, 22, 33);
    constexpr COLORREF kMenuTextColor = RGB(101, 111, 121);
    constexpr COLORREF kHighlightColor = RGB(204, 51, 17);
    std::vector<DisplayPlacementMenuBitmapPixel> pixels(kBitmapSize * kBitmapSize);
    PaintDisplayPlacementMenuBitmapPixels(
        pixels.data(), kBitmapSize, option, kMenuColor, kMenuTextColor, kHighlightColor);
    return pixels;
}

int CountPixels(
    const std::vector<DisplayPlacementMenuBitmapPixel>& pixels, DisplayPlacementMenuBitmapPixel expectedPixel) {
    return static_cast<int>(std::count(pixels.begin(), pixels.end(), expectedPixel));
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

TEST(DisplayMenuCheckmark, FullscreenCommittedConfigRequiresExpectedWallpaper) {
    const TargetMonitorInfo monitor = MakeTargetMonitor(1920, 1080);
    const AppConfig liveConfig = MakeDisplayConfig(1600, 900);
    AppConfig committed = MakeFullscreenWallpaperConfig("Panel", 1600, 900, monitor);
    DisplayMenuOption options[3]{};

    size_t count = BuildDisplayMenuOptionsForMonitor(
        liveConfig, MakeMonitor(1920, 1080), &committed.display, monitor, false, options, 3);

    ASSERT_EQ(count, 1u);
    EXPECT_TRUE(options[0].matchesCommittedConfig);

    committed.display.autohide = "top";
    count = BuildDisplayMenuOptionsForMonitor(
        liveConfig, MakeMonitor(1920, 1080), &committed.display, monitor, false, options, 3);

    ASSERT_EQ(count, 1u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);

    committed.display.autohide.clear();
    committed.display.wallpaper = "other.png";
    count = BuildDisplayMenuOptionsForMonitor(
        liveConfig, MakeMonitor(1920, 1080), &committed.display, monitor, false, options, 3);

    ASSERT_EQ(count, 1u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);
}

TEST(DisplayMenuCheckmark, EdgeCommittedConfigRequiresEmptyWallpaper) {
    const TargetMonitorInfo monitor = MakeTargetMonitor(1200, 1000);
    const AppConfig liveConfig = MakeDisplayConfig(1600, 900);
    AppConfig committed = liveConfig;
    committed.display.monitorName = "Panel";
    committed.display.scale = 0.75;
    committed.display.position = LogicalPointConfig{0, ScalePhysicalToLogical(325, 0.75)};
    committed.display.autohide = "bottom";
    committed.display.wallpaper.clear();
    DisplayMenuOption options[3]{};

    size_t count = BuildDisplayMenuOptionsForMonitor(
        liveConfig, MakeMonitor(1200, 1000), &committed.display, monitor, false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);
    EXPECT_TRUE(options[1].matchesCommittedConfig);

    committed.display.autohide = "top";
    committed.display.position = LogicalPointConfig{};
    count = BuildDisplayMenuOptionsForMonitor(
        liveConfig, MakeMonitor(1200, 1000), &committed.display, monitor, false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_TRUE(options[0].matchesCommittedConfig);
    EXPECT_FALSE(options[1].matchesCommittedConfig);

    committed.display.autohide.clear();
    count = BuildDisplayMenuOptionsForMonitor(
        liveConfig, MakeMonitor(1200, 1000), &committed.display, monitor, false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);
    EXPECT_FALSE(options[1].matchesCommittedConfig);

    committed.display.autohide = "bottom";
    committed.display.position = LogicalPointConfig{0, ScalePhysicalToLogical(325, 0.75)};
    committed.display.wallpaper = kDefaultBlankWallpaperFileName;
    count = BuildDisplayMenuOptionsForMonitor(
        liveConfig, MakeMonitor(1200, 1000), &committed.display, monitor, false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);
    EXPECT_FALSE(options[1].matchesCommittedConfig);
}

TEST(DisplayMenuCheckmark, LivePlacementDoesNotMoveCommittedCheckmark) {
    const TargetMonitorInfo monitor = MakeTargetMonitor(1200, 1000);
    AppConfig liveConfig = MakeDisplayConfig(1600, 900);
    liveConfig.display.monitorName = "Other";
    liveConfig.display.scale = 1.1;
    liveConfig.display.position = LogicalPointConfig{17, 23};
    AppConfig committed = MakeDisplayConfig(1600, 900);
    committed.display.monitorName = "Panel";
    committed.display.scale = 0.75;
    committed.display.position = LogicalPointConfig{0, ScalePhysicalToLogical(325, 0.75)};
    committed.display.autohide = "bottom";
    DisplayMenuOption options[3]{};

    const size_t count = BuildDisplayMenuOptionsForMonitor(
        liveConfig, MakeMonitor(1200, 1000), &committed.display, monitor, false, options, 3);

    ASSERT_EQ(count, 2u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);
    EXPECT_TRUE(options[1].matchesCommittedConfig);
}

TEST(DisplayMenuCheckmark, InconsistentCommittedPlacementChecksNothing) {
    const TargetMonitorInfo monitor = MakeTargetMonitor(1200, 1000);
    const AppConfig liveConfig = MakeDisplayConfig(1600, 900);
    AppConfig committed = liveConfig;
    committed.display.monitorName = "Panel";
    committed.display.scale = 0.75;
    committed.display.position = LogicalPointConfig{0, ScalePhysicalToLogical(325, 0.75)};
    committed.display.autohide = "bottom";
    DisplayMenuOption options[3]{};

    AppConfig wrongScale = committed;
    wrongScale.display.scale = 0.751;
    ASSERT_EQ(BuildDisplayMenuOptionsForMonitor(
                  liveConfig, MakeMonitor(1200, 1000), &wrongScale.display, monitor, false, options, 3),
        2u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);
    EXPECT_FALSE(options[1].matchesCommittedConfig);

    AppConfig wrongPosition = committed;
    wrongPosition.display.position = LogicalPointConfig{1, ScalePhysicalToLogical(325, 0.75)};
    ASSERT_EQ(BuildDisplayMenuOptionsForMonitor(
                  liveConfig, MakeMonitor(1200, 1000), &wrongPosition.display, monitor, false, options, 3),
        2u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);
    EXPECT_FALSE(options[1].matchesCommittedConfig);

    ASSERT_EQ(BuildDisplayMenuOptionsForMonitor(liveConfig,
                  MakeMonitor(1200, 1000),
                  &committed.display,
                  MakeTargetMonitorAt(1200, 0, 1200, 1000),
                  false,
                  options,
                  3),
        2u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);
    EXPECT_FALSE(options[1].matchesCommittedConfig);

    ASSERT_EQ(BuildDisplayMenuOptionsForMonitor(
                  liveConfig, MakeMonitor(1200, 1000), &committed.display, std::nullopt, false, options, 3),
        2u);
    EXPECT_FALSE(options[0].matchesCommittedConfig);
    EXPECT_FALSE(options[1].matchesCommittedConfig);
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

TEST(DisplayPlacementMenuBitmap, InactiveOptionUsesMenuBackground) {
    DisplayMenuOption option = MakeSchematicOption(DisplayPlacementMode::FullScreen, 1200, 1000, 1200, 1000);
    option.matchesCommittedConfig = false;

    const std::vector<DisplayPlacementMenuBitmapPixel> pixels = PaintMenuBitmapForTest(option);

    const auto menuBackground = OpaqueDisplayPlacementMenuBitmapPixel(RGB(11, 22, 33));
    const auto activeBackground =
        OpaqueDisplayPlacementMenuBitmapPixel(ResolveNativeThemeSelectedBackground(RGB(11, 22, 33), RGB(204, 51, 17)));
    EXPECT_GT(CountPixels(pixels, menuBackground), 0);
    EXPECT_EQ(CountPixels(pixels, activeBackground), 0);
}

TEST(DisplayPlacementMenuBitmap, ActiveOptionUsesTintedBackgroundAndPreservesPlacementSchematic) {
    DisplayMenuOption option = MakeSchematicOption(DisplayPlacementMode::Right, 1200, 1000, 563, 1000);
    option.matchesCommittedConfig = true;

    const std::vector<DisplayPlacementMenuBitmapPixel> pixels = PaintMenuBitmapForTest(option);

    const COLORREF activeBackgroundColor = ResolveNativeThemeSelectedBackground(RGB(11, 22, 33), RGB(204, 51, 17));
    const auto activeBackground = OpaqueDisplayPlacementMenuBitmapPixel(activeBackgroundColor);
    const auto placementFill =
        OpaqueDisplayPlacementMenuBitmapPixel(BlendNativeThemeColor(RGB(204, 51, 17), activeBackgroundColor, 32));
    EXPECT_GT(CountPixels(pixels, activeBackground), 0);
    EXPECT_GT(CountPixels(pixels, placementFill), 0);
    EXPECT_EQ(CountPixels(pixels, OpaqueDisplayPlacementMenuBitmapPixel(RGB(11, 22, 33))), 0);
}

TEST(DisplayPlacementMenuBitmap, ActiveBackgroundUsesProvidedMenuAndHighlightPalette) {
    DisplayMenuOption option = MakeSchematicOption(DisplayPlacementMode::FullScreen, 1200, 1000, 1200, 1000);
    option.matchesCommittedConfig = true;

    const std::vector<DisplayPlacementMenuBitmapPixel> pixels = PaintMenuBitmapForTest(option);

    const auto activeBackground =
        OpaqueDisplayPlacementMenuBitmapPixel(ResolveNativeThemeSelectedBackground(RGB(11, 22, 33), RGB(204, 51, 17)));
    EXPECT_GT(CountPixels(pixels, activeBackground), 0);
    EXPECT_EQ(CountPixels(pixels, OpaqueDisplayPlacementMenuBitmapPixel(RGB(0, 0, 0))), 0);
    EXPECT_EQ(CountPixels(pixels, OpaqueDisplayPlacementMenuBitmapPixel(RGB(255, 255, 255))), 0);
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

TEST(DisplayAspectResize, DragTargetAnchorsOppositeCornerForEveryCorner) {
    const SIZE layoutSize{1600, 900};

    const struct {
        DisplayResizeCorner corner;
        POINT anchor;
        POINT dragged;
    } cases[] = {
        {DisplayResizeCorner::TopLeft, POINT{1700, 1100}, POINT{100, 200}},
        {DisplayResizeCorner::TopRight, POINT{100, 1100}, POINT{1700, 200}},
        {DisplayResizeCorner::BottomLeft, POINT{1700, 200}, POINT{100, 1100}},
        {DisplayResizeCorner::BottomRight, POINT{100, 200}, POINT{1700, 1100}},
    };

    for (const auto& testCase : cases) {
        const DisplayAspectResizeTarget target =
            ComputeAspectResizeDragTarget(layoutSize, testCase.corner, testCase.anchor, testCase.dragged);

        EXPECT_NEAR(target.targetScale, 1.0, 0.000001);
        ExpectRect(target.targetClientRect, 100, 200, 1700, 1100);
    }
}

TEST(DisplayAspectResize, DragTargetClampsWithoutMovingOppositeCorner) {
    const DisplayAspectResizeTarget target = ComputeAspectResizeDragTarget(
        SIZE{1600, 900}, DisplayResizeCorner::TopLeft, POINT{1000, 1000}, POINT{1200, 1200});

    EXPECT_NEAR(target.targetScale, 0.1, 0.000001);
    ExpectRect(target.targetClientRect, 840, 910, 1000, 1000);
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
    EXPECT_TRUE(updated.display.autohide.empty());
}

TEST(DisplayConfiguration, EdgeConfigClearsWallpaperValueAndSetsAutohideSide) {
    const AppConfig config = MakeDisplayConfig(1600, 900);
    DisplayMenuOption options[2]{};
    ASSERT_EQ(BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1200, 1000), std::nullopt, false, options, 2), 2u);

    const AppConfig updated = BuildConfiguredDisplayConfig(config, options[1]);

    EXPECT_EQ(updated.display.monitorName, "Panel");
    EXPECT_EQ(updated.display.position, (LogicalPointConfig{0, ScalePhysicalToLogical(325, 0.75)}));
    EXPECT_NEAR(updated.display.scale, 0.75, 0.000001);
    EXPECT_TRUE(updated.display.wallpaper.empty());
    EXPECT_EQ(updated.display.autohide, "bottom");
}

TEST(DisplayAutohidePlacement, ResolvesConfiguredTargetAndRequiresExactClientRect) {
    AppConfig config = MakeDisplayConfig(1600, 900);
    DisplayMenuOption options[2]{};
    ASSERT_EQ(BuildDisplayMenuOptionsForMonitor(config, MakeMonitor(1200, 1000), std::nullopt, false, options, 2), 2u);
    config = BuildConfiguredDisplayConfig(config, options[1]);

    const DisplayMenuMonitorInfo monitor{"Panel", "Panel", MakeRect(1200, 1000), USER_DEFAULT_SCREEN_DPI};
    const std::optional<DisplayPlacementTarget> target =
        ComputeDisplayPlacementTarget(config, monitor, DisplayPlacementMode::Bottom);

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->autohide, "bottom");
    EXPECT_TRUE(DisplayPlacementTargetMatchesRect(*target, target->targetClientRect));
    RECT moved = target->targetClientRect;
    ++moved.left;
    ++moved.right;
    EXPECT_FALSE(DisplayPlacementTargetMatchesRect(*target, moved));
}

TEST(DisplayAutohidePlacement, IgnoresInvalidAutohideValue) {
    AppConfig config = MakeDisplayConfig(1600, 900);
    config.display.autohide = "middle";

    EXPECT_FALSE(DisplayPlacementModeFromAutohideValue(config.display.autohide).has_value());
}

TEST(DisplayConfiguration, PreviousWallpaperClearRequestsFollowPlacementAndMonitor) {
    const TargetMonitorInfo previousMonitor = MakeTargetMonitor(1920, 1080);
    AppConfig previous = MakeFullscreenWallpaperConfig("Panel", 1600, 900, previousMonitor);

    DisplayMenuOption fullscreenOption;
    fullscreenOption.writesWallpaper = true;
    fullscreenOption.monitorRect = MakeRect(1920, 1080);
    EXPECT_FALSE(ShouldClearPreviousDisplayWallpaper(previous, previousMonitor, fullscreenOption));
    EXPECT_TRUE(
        ShouldClearPreviousDisplayWallpaper(previous, MakeTargetMonitorAt(1920, 0, 1920, 1080), fullscreenOption));

    DisplayMenuOption edgeOption = fullscreenOption;
    edgeOption.writesWallpaper = false;
    EXPECT_TRUE(ShouldClearPreviousDisplayWallpaper(previous, previousMonitor, edgeOption));

    previous.display.wallpaper.clear();
    EXPECT_FALSE(ShouldClearPreviousDisplayWallpaper(previous, previousMonitor, fullscreenOption));
}

TEST(DisplayWallpaperOwnership, FullscreenConfigResolvesAsOwner) {
    const TargetMonitorInfo monitor = MakeTargetMonitor(1920, 1080);
    const AppConfig config = MakeFullscreenWallpaperConfig("Panel", 1600, 900, monitor);

    const std::optional<DisplayWallpaperOwner> owner = ResolveCommittedDisplayWallpaperOwner(config, monitor);

    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(owner->monitorName, "Panel");
    EXPECT_EQ(owner->wallpaper, kDefaultBlankWallpaperFileName);
    EXPECT_TRUE(RectsEqual(owner->monitorRect, monitor.rect));
}

TEST(DisplayWallpaperOwnership, MovedOrResizedWallpaperConfigIsNotOwner) {
    const TargetMonitorInfo monitor = MakeTargetMonitor(1920, 1080);
    AppConfig moved = MakeFullscreenWallpaperConfig("Panel", 1600, 900, monitor);
    moved.display.position = LogicalPointConfig{1, 0};

    AppConfig resized = MakeFullscreenWallpaperConfig("Panel", 1600, 900, monitor);
    resized.display.scale = 1.0;

    EXPECT_FALSE(ResolveCommittedDisplayWallpaperOwner(moved, monitor).has_value());
    EXPECT_TRUE(NormalizeCommittedDisplayWallpaperConfig(moved, monitor).display.wallpaper.empty());
    EXPECT_FALSE(ResolveCommittedDisplayWallpaperOwner(resized, monitor).has_value());
    EXPECT_TRUE(NormalizeCommittedDisplayWallpaperConfig(resized, monitor).display.wallpaper.empty());
}

TEST(DisplayWallpaperOwnership, SameMonitorFullscreenTransitionDoesNotClear) {
    const TargetMonitorInfo monitor = MakeTargetMonitor(1920, 1080);
    const AppConfig previous = MakeFullscreenWallpaperConfig("Panel", 1600, 900, monitor);
    const AppConfig next = MakeFullscreenWallpaperConfig("Panel", 1600, 900, monitor);

    EXPECT_FALSE(ShouldClearCommittedDisplayWallpaper(ResolveCommittedDisplayWallpaperOwner(previous, monitor),
        ResolveCommittedDisplayWallpaperOwner(next, monitor)));
}

TEST(DisplayWallpaperOwnership, FullscreenMonitorTransitionClearsPreviousAndOwnsNext) {
    const TargetMonitorInfo monitorA = MakeTargetMonitor(1920, 1080);
    const TargetMonitorInfo monitorB = MakeTargetMonitorAt(1920, 0, 1920, 1080);
    const AppConfig previous = MakeFullscreenWallpaperConfig("Panel A", 1600, 900, monitorA);
    const AppConfig next = MakeFullscreenWallpaperConfig("Panel B", 1600, 900, monitorB);

    const std::optional<DisplayWallpaperOwner> nextOwner = ResolveCommittedDisplayWallpaperOwner(next, monitorB);

    ASSERT_TRUE(nextOwner.has_value());
    EXPECT_TRUE(
        ShouldClearCommittedDisplayWallpaper(ResolveCommittedDisplayWallpaperOwner(previous, monitorA), nextOwner));
}

TEST(DisplayWallpaperOwnership, FullscreenToEdgeClearsPreviousAndSavesEmptyWallpaper) {
    const TargetMonitorInfo monitor = MakeTargetMonitor(1200, 1000);
    const AppConfig previous = MakeFullscreenWallpaperConfig("Panel", 1200, 1000, monitor);
    AppConfig edge = previous;
    edge.display.position = LogicalPointConfig{0, 100};

    const AppConfig normalized = NormalizeCommittedDisplayWallpaperConfig(edge, monitor);

    EXPECT_TRUE(normalized.display.wallpaper.empty());
    EXPECT_TRUE(ShouldClearCommittedDisplayWallpaper(ResolveCommittedDisplayWallpaperOwner(previous, monitor),
        ResolveCommittedDisplayWallpaperOwner(normalized, monitor)));
}

TEST(DisplayWallpaperOwnership, CommittedOwnerDrivesClearAfterManualMove) {
    const TargetMonitorInfo monitorA = MakeTargetMonitor(1920, 1080);
    const TargetMonitorInfo monitorB = MakeTargetMonitorAt(1920, 0, 1920, 1080);
    const AppConfig committedOwnerConfig = MakeFullscreenWallpaperConfig("Panel A", 1600, 900, monitorA);
    AppConfig manuallyMoved = committedOwnerConfig;
    manuallyMoved.display.monitorName = "Panel B";
    manuallyMoved.display.position = LogicalPointConfig{10, 10};
    const AppConfig configuredB = MakeFullscreenWallpaperConfig("Panel B", 1600, 900, monitorB);

    EXPECT_FALSE(ResolveCommittedDisplayWallpaperOwner(manuallyMoved, monitorB).has_value());
    EXPECT_TRUE(
        ShouldClearCommittedDisplayWallpaper(ResolveCommittedDisplayWallpaperOwner(committedOwnerConfig, monitorA),
            ResolveCommittedDisplayWallpaperOwner(configuredB, monitorB)));
}
