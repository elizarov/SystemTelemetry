#include <array>
#include <gtest/gtest.h>

#include "dashboard/dashboard_titlebar.h"
#include "dashboard/dashboard_window_chrome.h"
#include "dashboard/native_theme_colors.h"

namespace {

void ExpectRect(const RECT& rect, int left, int top, int right, int bottom) {
    EXPECT_EQ(rect.left, left);
    EXPECT_EQ(rect.top, top);
    EXPECT_EQ(rect.right, right);
    EXPECT_EQ(rect.bottom, bottom);
}

bool RectUsable(const RECT& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

void ExpectNoOverlappingControlRects(const DashboardTitlebarControlLayout& layout) {
    const std::array<RECT, 7> rects{layout.appMenuRect,
        layout.themeComboRect,
        layout.layoutComboRect,
        layout.editLayoutRect,
        layout.displayRect,
        layout.closeRect,
        layout.titleTextRect};
    for (size_t i = 0; i < rects.size(); ++i) {
        if (!RectUsable(rects[i])) {
            continue;
        }
        for (size_t j = i + 1; j < rects.size(); ++j) {
            if (!RectUsable(rects[j])) {
                continue;
            }
            RECT intersection{};
            EXPECT_FALSE(IntersectRect(&intersection, &rects[i], &rects[j]))
                << "rect " << i << " overlapped rect " << j;
        }
    }
}

DashboardTitlebarControlMetrics TestControlMetrics() {
    return DashboardTitlebarControlMetrics{36, 6, 8, 22, 58, 78, 76, 112};
}

}  // namespace

TEST(DashboardTitlebarGeometry, AllowsTitlebarWhenNativeWindowFitsMonitor) {
    const RECT client{100, 100, 500, 300};
    const RECT monitor{0, 0, 800, 600};
    const DashboardTitlebarFrameMargins margins{8, 32, 8, 8};

    const DashboardTitlebarGeometry geometry = ResolveDashboardTitlebarGeometry(client, monitor, margins);

    EXPECT_TRUE(geometry.canShow);
    ExpectRect(geometry.windowRect, 92, 68, 508, 308);
    ExpectRect(geometry.virtualHoverRect, 100, 68, 500, 100);
}

TEST(DashboardTitlebarGeometry, UsesDashboardClientWidthForHoverBand) {
    const RECT client{100, 100, 500, 300};
    const RECT monitor{0, 0, 800, 600};
    const DashboardTitlebarFrameMargins margins{20, 32, 40, 8};

    const DashboardTitlebarGeometry geometry = ResolveDashboardTitlebarGeometry(client, monitor, margins);

    ASSERT_TRUE(geometry.canShow);
    ExpectRect(geometry.virtualHoverRect, 100, 68, 500, 100);
}

TEST(DashboardTitlebarGeometry, SuppressesTitlebarWhenDashboardFillsMonitor) {
    const RECT client{0, 0, 800, 600};
    const RECT monitor{0, 0, 800, 600};
    const DashboardTitlebarFrameMargins margins{8, 32, 8, 8};

    const DashboardTitlebarGeometry geometry = ResolveDashboardTitlebarGeometry(client, monitor, margins);

    EXPECT_FALSE(geometry.canShow);
}

TEST(DashboardTitlebarGeometry, SuppressesTitlebarWhenNoRoomAboveOnSameMonitor) {
    const RECT monitor{0, 0, 800, 600};
    const DashboardTitlebarFrameMargins margins{8, 32, 8, 8};

    EXPECT_FALSE(ResolveDashboardTitlebarGeometry(RECT{100, 20, 500, 220}, monitor, margins).canShow);
    EXPECT_TRUE(ResolveDashboardTitlebarGeometry(RECT{100, 32, 500, 220}, monitor, margins).canShow);
}

TEST(DashboardTitlebarGeometry, SuppressesTitlebarWhenDashboardTouchesMonitorEdge) {
    const RECT monitor{0, 0, 800, 600};
    const DashboardTitlebarFrameMargins margins{8, 32, 8, 8};

    EXPECT_FALSE(ResolveDashboardTitlebarGeometry(RECT{0, 100, 400, 300}, monitor, margins).canShow);
    EXPECT_FALSE(ResolveDashboardTitlebarGeometry(RECT{100, 0, 500, 300}, monitor, margins).canShow);
    EXPECT_FALSE(ResolveDashboardTitlebarGeometry(RECT{400, 100, 800, 300}, monitor, margins).canShow);
    EXPECT_FALSE(ResolveDashboardTitlebarGeometry(RECT{100, 300, 500, 600}, monitor, margins).canShow);
}

TEST(DashboardTitlebarGeometry, AllowsTitlebarWhenNativeSideFrameCrossesMonitorSideButClientIsInset) {
    const RECT monitor{0, 0, 800, 600};
    const DashboardTitlebarFrameMargins margins{8, 32, 8, 8};

    EXPECT_TRUE(ResolveDashboardTitlebarGeometry(RECT{2, 100, 402, 300}, monitor, margins).canShow);
    EXPECT_TRUE(ResolveDashboardTitlebarGeometry(RECT{400, 100, 798, 300}, monitor, margins).canShow);
}

TEST(DashboardTitlebarGeometry, KeepsFrameGeometryAvailableWhenMonitorFitSuppressesHover) {
    const RECT client{100, 20, 500, 220};
    const DashboardTitlebarFrameMargins margins{8, 32, 8, 8};

    const DashboardTitlebarGeometry geometry = ResolveDashboardTitlebarFrameGeometry(client, margins);

    EXPECT_TRUE(geometry.canShow);
    ExpectRect(geometry.windowRect, 92, -12, 508, 228);
    ExpectRect(geometry.virtualHoverRect, 100, -12, 500, 20);
}

TEST(DashboardTitlebarGeometry, AllowsTitlebarWhenOnlyNativeBottomFrameCrossesMonitorEdgeAndClientIsInset) {
    const RECT monitor{0, 0, 800, 600};
    const DashboardTitlebarFrameMargins margins{8, 32, 8, 8};

    const DashboardTitlebarGeometry geometry =
        ResolveDashboardTitlebarGeometry(RECT{100, 100, 500, 596}, monitor, margins);

    EXPECT_TRUE(geometry.canShow);
    ExpectRect(geometry.windowRect, 92, 68, 508, 604);
    ExpectRect(geometry.virtualHoverRect, 100, 68, 500, 100);
}

TEST(DashboardTitlebarGeometry, PreservesClientRectThroughAdjustedMargins) {
    const RECT adjusted{-8, -32, 408, 208};
    const DashboardTitlebarFrameMargins margins = DashboardTitlebarFrameMarginsFromAdjustedRect(adjusted, 400, 200);
    const RECT client{100, 100, 500, 300};
    const RECT monitor{0, 0, 800, 600};

    const DashboardTitlebarGeometry geometry = ResolveDashboardTitlebarGeometry(client, monitor, margins);

    ASSERT_TRUE(geometry.canShow);
    const RECT reconstructedClient{geometry.windowRect.left + margins.left,
        geometry.windowRect.top + margins.top,
        geometry.windowRect.right - margins.right,
        geometry.windowRect.bottom - margins.bottom};
    ExpectRect(reconstructedClient, 100, 100, 500, 300);
}

TEST(DashboardTitlebarPalette, DerivesButtonColorsFromBaseColors) {
    const DashboardTitlebarPalette palette =
        ResolveDashboardTitlebarPaletteFromBaseColors(RGB(240, 240, 240), RGB(40, 40, 40));

    EXPECT_EQ(palette.background, RGB(240, 240, 240));
    EXPECT_EQ(palette.text, RGB(40, 40, 40));
    EXPECT_EQ(palette.buttonGlyph, RGB(40, 40, 40));
    EXPECT_EQ(palette.buttonHover, RGB(228, 228, 228));
    EXPECT_EQ(palette.buttonPressed, RGB(216, 216, 216));
    EXPECT_EQ(palette.buttonSelected, ResolveNativeThemeSelectedBackground(RGB(240, 240, 240), RGB(40, 40, 40)));
}

TEST(DashboardTitlebarPalette, DerivesDarkButtonColorsFromBaseColors) {
    const DashboardTitlebarPalette palette =
        ResolveDashboardTitlebarPaletteFromBaseColors(RGB(32, 32, 32), RGB(255, 255, 255));

    EXPECT_EQ(palette.background, RGB(32, 32, 32));
    EXPECT_EQ(palette.text, RGB(255, 255, 255));
    EXPECT_EQ(palette.buttonGlyph, RGB(255, 255, 255));
    EXPECT_EQ(palette.buttonHover, RGB(45, 45, 45));
    EXPECT_EQ(palette.buttonPressed, RGB(58, 58, 58));
    EXPECT_EQ(palette.buttonSelected, ResolveNativeThemeSelectedBackground(RGB(32, 32, 32), RGB(255, 255, 255)));
}

TEST(DashboardTitlebarPalette, AcceptsNativeSelectedBackgroundSharedWithMenuIcons) {
    const COLORREF selectedBackground = ResolveNativeThemeSelectedBackground(RGB(11, 22, 33), RGB(204, 51, 17));
    const DashboardTitlebarPalette palette =
        ResolveDashboardTitlebarPaletteFromBaseColors(RGB(240, 240, 240), RGB(40, 40, 40), selectedBackground);

    EXPECT_EQ(palette.buttonSelected, selectedBackground);
}

TEST(DashboardTitlebarChrome, ScalesTopCornerRadiusWithDpi) {
    EXPECT_EQ(ResolveDashboardTitlebarCornerRadius(96), 8);
    EXPECT_EQ(ResolveDashboardTitlebarCornerRadius(120), 10);
    EXPECT_EQ(ResolveDashboardTitlebarCornerRadius(144), 12);
    EXPECT_EQ(ResolveDashboardTitlebarCornerRadius(0), 8);
}

TEST(DashboardTitlebarChrome, KeepsResizeHitSizeBelowRoundedCornerRadius) {
    EXPECT_LT(ResolveDashboardTitlebarResizeCornerHitSize(96), ResolveDashboardTitlebarCornerRadius(96));
    EXPECT_EQ(ResolveDashboardTitlebarResizeCornerHitSize(96), 7);
    EXPECT_EQ(ResolveDashboardTitlebarResizeCornerHitSize(120), 9);
    EXPECT_EQ(ResolveDashboardTitlebarResizeCornerHitSize(144), 11);
    EXPECT_EQ(ResolveDashboardTitlebarResizeCornerHitSize(0), 7);
}

TEST(DashboardTitlebarChrome, NullWindowReportsFailureWithoutThrowing) {
    const DashboardTitlebarChromeResult result = ApplyDashboardTitlebarChrome(nullptr, true);

    EXPECT_FALSE(DashboardTitlebarChromeSucceeded(result));
    EXPECT_EQ(result.cornerPreference, E_HANDLE);
    EXPECT_EQ(result.borderColor, E_HANDLE);
    EXPECT_EQ(result.captionColor, E_HANDLE);
    EXPECT_EQ(result.textColor, E_HANDLE);
    EXPECT_EQ(result.darkMode, E_HANDLE);
}

TEST(DashboardTitlebarResizeHitRects, UsesSmallSquareTopCornerRegions) {
    const DashboardTitlebarResizeHitRects hitRects =
        ResolveDashboardTitlebarResizeHitRects(RECT{0, 0, 400, 32}, ResolveDashboardTitlebarResizeCornerHitSize(96));

    ExpectRect(hitRects.topLeft, 0, 0, 7, 7);
    ExpectRect(hitRects.topRight, 393, 0, 400, 7);
}

TEST(DashboardTitlebarResizeHitRects, ClampsToAvailableTitlebarSize) {
    const DashboardTitlebarResizeHitRects hitRects =
        ResolveDashboardTitlebarResizeHitRects(RECT{10, 20, 14, 24}, ResolveDashboardTitlebarResizeCornerHitSize(96));

    ExpectRect(hitRects.topLeft, 10, 20, 14, 24);
    ExpectRect(hitRects.topRight, 10, 20, 14, 24);
}

TEST(DashboardTitlebarControlLayout, FullWidthShowsAllControls) {
    const DashboardTitlebarControlLayout layout =
        ResolveDashboardTitlebarControlLayout(RECT{0, 0, 422, 32}, TestControlMetrics());

    EXPECT_TRUE(RectUsable(layout.appMenuRect));
    EXPECT_TRUE(RectUsable(layout.themeComboRect));
    EXPECT_TRUE(RectUsable(layout.layoutComboRect));
    EXPECT_TRUE(RectUsable(layout.editLayoutRect));
    EXPECT_TRUE(RectUsable(layout.displayRect));
    EXPECT_TRUE(RectUsable(layout.closeRect));
    EXPECT_TRUE(RectUsable(layout.titleTextRect));
    EXPECT_LT(layout.themeComboRect.left, layout.layoutComboRect.left);
    EXPECT_LT(layout.layoutComboRect.left, layout.editLayoutRect.left);
    EXPECT_LT(layout.editLayoutRect.left, layout.displayRect.left);
    EXPECT_LT(layout.displayRect.left, layout.closeRect.left);
    ExpectNoOverlappingControlRects(layout);
}

TEST(DashboardTitlebarControlLayout, DropsControlsFromThemeThroughDisplay) {
    const DashboardTitlebarControlMetrics metrics = TestControlMetrics();

    const DashboardTitlebarControlLayout noTheme = ResolveDashboardTitlebarControlLayout(RECT{0, 0, 310, 32}, metrics);
    EXPECT_FALSE(RectUsable(noTheme.themeComboRect));
    EXPECT_TRUE(RectUsable(noTheme.layoutComboRect));
    EXPECT_TRUE(RectUsable(noTheme.editLayoutRect));
    EXPECT_TRUE(RectUsable(noTheme.displayRect));
    EXPECT_TRUE(RectUsable(noTheme.closeRect));
    ExpectNoOverlappingControlRects(noTheme);

    const DashboardTitlebarControlLayout noLayout = ResolveDashboardTitlebarControlLayout(RECT{0, 0, 220, 32}, metrics);
    EXPECT_FALSE(RectUsable(noLayout.themeComboRect));
    EXPECT_FALSE(RectUsable(noLayout.layoutComboRect));
    EXPECT_TRUE(RectUsable(noLayout.editLayoutRect));
    EXPECT_TRUE(RectUsable(noLayout.displayRect));
    EXPECT_TRUE(RectUsable(noLayout.closeRect));
    ExpectNoOverlappingControlRects(noLayout);

    const DashboardTitlebarControlLayout noEdit = ResolveDashboardTitlebarControlLayout(RECT{0, 0, 125, 32}, metrics);
    EXPECT_FALSE(RectUsable(noEdit.themeComboRect));
    EXPECT_FALSE(RectUsable(noEdit.layoutComboRect));
    EXPECT_FALSE(RectUsable(noEdit.editLayoutRect));
    EXPECT_TRUE(RectUsable(noEdit.displayRect));
    EXPECT_TRUE(RectUsable(noEdit.closeRect));
    ExpectNoOverlappingControlRects(noEdit);

    const DashboardTitlebarControlLayout noDisplay =
        ResolveDashboardTitlebarControlLayout(RECT{0, 0, 100, 32}, metrics);
    EXPECT_FALSE(RectUsable(noDisplay.themeComboRect));
    EXPECT_FALSE(RectUsable(noDisplay.layoutComboRect));
    EXPECT_FALSE(RectUsable(noDisplay.editLayoutRect));
    EXPECT_FALSE(RectUsable(noDisplay.displayRect));
    EXPECT_TRUE(RectUsable(noDisplay.closeRect));
    ExpectNoOverlappingControlRects(noDisplay);
}

TEST(DashboardTitlebarControlLayout, CloseWinsWhenAppMenuWouldCollide) {
    const DashboardTitlebarControlLayout layout =
        ResolveDashboardTitlebarControlLayout(RECT{0, 0, 70, 32}, TestControlMetrics());

    EXPECT_FALSE(RectUsable(layout.appMenuRect));
    EXPECT_TRUE(RectUsable(layout.closeRect));
    ExpectNoOverlappingControlRects(layout);
}

TEST(DashboardTitlebarTooltip, ResolvesControlKeys) {
    const RECT appMenu{0, 0, 36, 32};
    const RECT layout{100, 5, 178, 27};
    const RECT theme{184, 5, 296, 27};
    const RECT editLayout{302, 0, 338, 32};
    const RECT display{344, 0, 380, 32};
    const RECT close{386, 0, 422, 32};

    EXPECT_STREQ(
        ResolveDashboardTitlebarTooltipTarget(POINT{10, 10}, appMenu, layout, theme, editLayout, display, close)
            .localizationKey,
        "titlebar.app_menu");
    EXPECT_STREQ(
        ResolveDashboardTitlebarTooltipTarget(POINT{120, 10}, appMenu, layout, theme, editLayout, display, close)
            .localizationKey,
        "titlebar.layout");
    EXPECT_STREQ(
        ResolveDashboardTitlebarTooltipTarget(POINT{200, 10}, appMenu, layout, theme, editLayout, display, close)
            .localizationKey,
        "titlebar.theme");
    EXPECT_STREQ(
        ResolveDashboardTitlebarTooltipTarget(POINT{320, 10}, appMenu, layout, theme, editLayout, display, close)
            .localizationKey,
        "titlebar.edit_layout");
    EXPECT_STREQ(
        ResolveDashboardTitlebarTooltipTarget(POINT{360, 10}, appMenu, layout, theme, editLayout, display, close)
            .localizationKey,
        "titlebar.display");
    EXPECT_STREQ(
        ResolveDashboardTitlebarTooltipTarget(POINT{400, 10}, appMenu, layout, theme, editLayout, display, close)
            .localizationKey,
        "titlebar.close");
}

TEST(DashboardTitlebarTooltip, SkipsUnavailableControls) {
    const RECT empty{};
    const RECT close{386, 0, 422, 32};

    const DashboardTitlebarTooltipTarget missing =
        ResolveDashboardTitlebarTooltipTarget(POINT{120, 10}, empty, empty, empty, empty, empty, close);
    EXPECT_EQ(missing.control, DashboardTitlebarTooltipControl::None);
    EXPECT_STREQ(missing.localizationKey, "");

    const DashboardTitlebarTooltipTarget available =
        ResolveDashboardTitlebarTooltipTarget(POINT{400, 10}, empty, empty, empty, empty, empty, close);
    EXPECT_EQ(available.control, DashboardTitlebarTooltipControl::Close);
    EXPECT_STREQ(available.localizationKey, "titlebar.close");
}
