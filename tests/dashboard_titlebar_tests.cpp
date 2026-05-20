#include <gtest/gtest.h>

#include "dashboard/dashboard_titlebar.h"
#include "dashboard/dashboard_window_chrome.h"

namespace {

void ExpectRect(const RECT& rect, int left, int top, int right, int bottom) {
    EXPECT_EQ(rect.left, left);
    EXPECT_EQ(rect.top, top);
    EXPECT_EQ(rect.right, right);
    EXPECT_EQ(rect.bottom, bottom);
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

TEST(DashboardTitlebarGeometry, SuppressesTitlebarWhenNativeFrameCrossesMonitorTopOrSideEdge) {
    const RECT monitor{0, 0, 800, 600};
    const DashboardTitlebarFrameMargins margins{8, 32, 8, 8};

    EXPECT_FALSE(ResolveDashboardTitlebarGeometry(RECT{100, 20, 500, 220}, monitor, margins).canShow);
    EXPECT_FALSE(ResolveDashboardTitlebarGeometry(RECT{2, 100, 402, 300}, monitor, margins).canShow);
    EXPECT_FALSE(ResolveDashboardTitlebarGeometry(RECT{400, 100, 798, 300}, monitor, margins).canShow);
}

TEST(DashboardTitlebarGeometry, AllowsTitlebarWhenOnlyNativeBottomFrameCrossesMonitorEdge) {
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
    EXPECT_EQ(palette.buttonHover, RGB(216, 216, 216));
    EXPECT_EQ(palette.buttonPressed, RGB(196, 196, 196));
}

TEST(DashboardTitlebarPalette, DerivesDarkButtonColorsFromBaseColors) {
    const DashboardTitlebarPalette palette =
        ResolveDashboardTitlebarPaletteFromBaseColors(RGB(32, 32, 32), RGB(255, 255, 255));

    EXPECT_EQ(palette.background, RGB(32, 32, 32));
    EXPECT_EQ(palette.text, RGB(255, 255, 255));
    EXPECT_EQ(palette.buttonGlyph, RGB(255, 255, 255));
    EXPECT_EQ(palette.buttonHover, RGB(58, 58, 58));
    EXPECT_EQ(palette.buttonPressed, RGB(81, 81, 81));
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
