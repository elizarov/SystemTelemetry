#include "dashboard/dashboard_window_chrome.h"

#include <algorithm>
#include <dwmapi.h>

namespace {

constexpr DWORD kDwmUseImmersiveDarkModeAttribute = 20;
constexpr DWORD kDwmWindowCornerPreferenceAttribute = 33;
constexpr DWORD kDwmBorderColorAttribute = 34;
constexpr DWORD kDwmCaptionColorAttribute = 35;
constexpr DWORD kDwmTextColorAttribute = 36;

constexpr UINT kDwmCornerPreferenceDoNotRound = 1;
constexpr UINT kDwmCornerPreferenceRound = 2;
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;

int ColorChannel(COLORREF color, int shift) {
    return static_cast<int>((color >> shift) & 0xFF);
}

COLORREF BlendColor(COLORREF from, COLORREF to, int toPercent) {
    const int clampedPercent = std::clamp(toPercent, 0, 100);
    const auto blend = [clampedPercent](int fromChannel, int toChannel) {
        return static_cast<BYTE>((fromChannel * (100 - clampedPercent) + toChannel * clampedPercent) / 100);
    };
    return RGB(blend(ColorChannel(from, 0), ColorChannel(to, 0)),
        blend(ColorChannel(from, 8), ColorChannel(to, 8)),
        blend(ColorChannel(from, 16), ColorChannel(to, 16)));
}

bool SystemAppsUseLightTheme() {
    DWORD value = 1;
    DWORD valueSize = sizeof(value);
    const LSTATUS status = RegGetValueA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        "AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &valueSize);
    return status != ERROR_SUCCESS || value != 0;
}

bool HighContrastEnabled() {
    HIGHCONTRASTA highContrast{};
    highContrast.cbSize = sizeof(highContrast);
    return SystemParametersInfoA(SPI_GETHIGHCONTRAST, highContrast.cbSize, &highContrast, 0) != FALSE &&
           (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

template <typename Value> HRESULT SetDwmAttribute(HWND hwnd, DWORD attribute, const Value& value) {
    return DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
}

}  // namespace

DashboardTitlebarPalette ResolveDashboardTitlebarPalette(HWND) {
    if (HighContrastEnabled()) {
        return ResolveDashboardTitlebarPaletteFromBaseColors(GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT));
    }

    if (!SystemAppsUseLightTheme()) {
        return ResolveDashboardTitlebarPaletteFromBaseColors(RGB(32, 32, 32), RGB(255, 255, 255));
    }

    return ResolveDashboardTitlebarPaletteFromBaseColors(GetSysColor(COLOR_3DFACE), GetSysColor(COLOR_WINDOWTEXT));
}

DashboardTitlebarPalette ResolveDashboardTitlebarPaletteFromBaseColors(COLORREF background, COLORREF text) {
    DashboardTitlebarPalette palette;
    palette.background = background;
    palette.text = text;
    palette.buttonHover = BlendColor(background, text, 12);
    palette.buttonPressed = BlendColor(background, text, 22);
    palette.buttonGlyph = text;
    return palette;
}

DashboardTitlebarChromeResult ApplyDashboardTitlebarChrome(HWND hwnd, bool titlebarVisible) {
    DashboardTitlebarChromeResult result;
    if (hwnd == nullptr) {
        result.cornerPreference = E_HANDLE;
        result.borderColor = E_HANDLE;
        result.captionColor = E_HANDLE;
        result.textColor = E_HANDLE;
        result.darkMode = E_HANDLE;
        return result;
    }

    const UINT cornerPreference = titlebarVisible ? kDwmCornerPreferenceRound : kDwmCornerPreferenceDoNotRound;
    const COLORREF defaultColor = kDwmColorDefault;
    const BOOL honorDarkMode = titlebarVisible ? TRUE : FALSE;
    result.cornerPreference = SetDwmAttribute(hwnd, kDwmWindowCornerPreferenceAttribute, cornerPreference);
    result.borderColor = SetDwmAttribute(hwnd, kDwmBorderColorAttribute, defaultColor);
    result.captionColor = SetDwmAttribute(hwnd, kDwmCaptionColorAttribute, defaultColor);
    result.textColor = SetDwmAttribute(hwnd, kDwmTextColorAttribute, defaultColor);
    result.darkMode = SetDwmAttribute(hwnd, kDwmUseImmersiveDarkModeAttribute, honorDarkMode);
    return result;
}

bool DashboardTitlebarChromeSucceeded(const DashboardTitlebarChromeResult& result) {
    return SUCCEEDED(result.cornerPreference) && SUCCEEDED(result.borderColor) && SUCCEEDED(result.captionColor) &&
           SUCCEEDED(result.textColor) && SUCCEEDED(result.darkMode);
}
