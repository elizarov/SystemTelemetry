#pragma once

#include <windows.h>

struct DashboardTitlebarPalette {
    COLORREF background = RGB(240, 240, 240);
    COLORREF text = RGB(32, 32, 32);
    COLORREF buttonHover = RGB(224, 224, 224);
    COLORREF buttonPressed = RGB(204, 204, 204);
    COLORREF buttonGlyph = RGB(32, 32, 32);
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
int ResolveDashboardTitlebarCornerRadius(UINT dpi);
DashboardCloseButtonColors ResolveDashboardCloseButtonColors(HWND hwnd, bool pressed);
DashboardTitlebarChromeResult ApplyDashboardTitlebarChrome(HWND hwnd, bool titlebarVisible);
bool DashboardTitlebarChromeSucceeded(const DashboardTitlebarChromeResult& result);
