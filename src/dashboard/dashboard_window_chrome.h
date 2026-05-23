#pragma once

#include <windows.h>

struct DashboardTitlebarPalette {
    COLORREF background = RGB(243, 243, 243);
    COLORREF text = RGB(0, 0, 0);
    COLORREF buttonHover = RGB(228, 228, 228);
    COLORREF buttonPressed = RGB(213, 213, 213);
    COLORREF buttonSelected = RGB(224, 233, 244);
    COLORREF buttonGlyph = RGB(0, 0, 0);
};

struct DashboardTitlebarChromeResult {
    HRESULT cornerPreference = S_OK;
    HRESULT borderColor = S_OK;
    HRESULT captionColor = S_OK;
    HRESULT textColor = S_OK;
    HRESULT darkMode = S_OK;
};

struct DashboardCloseButtonColors {
    COLORREF background = RGB(232, 17, 35);
    COLORREF glyph = RGB(255, 255, 255);
};

DashboardTitlebarPalette ResolveDashboardTitlebarPalette(HWND hwnd);
DashboardTitlebarPalette ResolveDashboardTitlebarPaletteFromBaseColors(COLORREF background, COLORREF text);
DashboardTitlebarPalette ResolveDashboardTitlebarPaletteFromBaseColors(
    COLORREF background, COLORREF text, COLORREF selectedBackground);
int ResolveDashboardTitlebarCornerRadius(UINT dpi);
int ResolveDashboardTitlebarResizeCornerHitSize(UINT dpi);
DashboardCloseButtonColors ResolveDashboardCloseButtonColors(HWND hwnd, bool pressed);
DashboardTitlebarChromeResult ApplyDashboardTitlebarChrome(HWND hwnd, bool titlebarVisible);
bool DashboardTitlebarChromeSucceeded(const DashboardTitlebarChromeResult& result);
