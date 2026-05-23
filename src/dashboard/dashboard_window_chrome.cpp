#include "dashboard/dashboard_window_chrome.h"

#include <algorithm>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>

#include "dashboard/native_theme_colors.h"

namespace {

constexpr DWORD kDwmUseImmersiveDarkModeAttribute = 20;
constexpr DWORD kDwmWindowCornerPreferenceAttribute = 33;
constexpr DWORD kDwmBorderColorAttribute = 34;
constexpr DWORD kDwmCaptionColorAttribute = 35;
constexpr DWORD kDwmTextColorAttribute = 36;

constexpr UINT kDwmCornerPreferenceDoNotRound = 1;
constexpr UINT kDwmCornerPreferenceRound = 2;
constexpr int kNativeTitlebarCornerRadiusLogical = 8;
constexpr UINT kDefaultDpi = 96;
constexpr UINT kMaxReasonableDpi = 960;
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;
constexpr wchar_t kWindowThemeClassName[] = L"WINDOW";  // UxTheme exposes only UTF-16 class names.
constexpr COLORREF kModernLightCaptionBackground = RGB(243, 243, 243);
constexpr COLORREF kModernLightCaptionText = RGB(0, 0, 0);
constexpr COLORREF kModernDarkCaptionBackground = RGB(32, 32, 32);
constexpr COLORREF kModernDarkCaptionText = RGB(255, 255, 255);
constexpr COLORREF kDefaultCloseButtonHoverColor = RGB(232, 17, 35);
constexpr COLORREF kDefaultCloseButtonPressedColor = RGB(196, 43, 28);
constexpr COLORREF kDefaultCloseButtonGlyphColor = RGB(255, 255, 255);
constexpr int kTitlebarButtonHoverBlendPercent = 6;
constexpr int kTitlebarButtonPressedBlendPercent = 12;

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

bool LooksLikeCloseActionColor(COLORREF color) {
    const int red = ColorChannel(color, 0);
    const int green = ColorChannel(color, 8);
    const int blue = ColorChannel(color, 16);
    return red >= 160 && green <= 140 && blue <= 160 && red >= green + 40;
}

template <typename Value> HRESULT SetDwmAttribute(HWND hwnd, DWORD attribute, const Value& value) {
    return DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
}

}  // namespace

DashboardTitlebarPalette ResolveDashboardTitlebarPalette(HWND) {
    const COLORREF selectedBackground =
        ResolveNativeThemeSelectedBackground(GetSysColor(COLOR_MENU), GetSysColor(COLOR_HIGHLIGHT));
    if (HighContrastEnabled()) {
        return ResolveDashboardTitlebarPaletteFromBaseColors(
            GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT), selectedBackground);
    }

    // UxTheme's WINDOW caption fill hint can still report legacy accent colors on Windows 11, while DWM renders the
    // modern neutral caption surface. Use the same neutral fallback family for the probe that covers that native caption.
    const bool lightTheme = SystemAppsUseLightTheme();
    return ResolveDashboardTitlebarPaletteFromBaseColors(
        lightTheme ? kModernLightCaptionBackground : kModernDarkCaptionBackground,
        lightTheme ? kModernLightCaptionText : kModernDarkCaptionText,
        selectedBackground);
}

DashboardTitlebarPalette ResolveDashboardTitlebarPaletteFromBaseColors(COLORREF background, COLORREF text) {
    return ResolveDashboardTitlebarPaletteFromBaseColors(
        background, text, ResolveNativeThemeSelectedBackground(background, text));
}

DashboardTitlebarPalette ResolveDashboardTitlebarPaletteFromBaseColors(
    COLORREF background, COLORREF text, COLORREF selectedBackground) {
    DashboardTitlebarPalette palette;
    palette.background = background;
    palette.text = text;
    palette.buttonHover = BlendColor(background, text, kTitlebarButtonHoverBlendPercent);
    palette.buttonPressed = BlendColor(background, text, kTitlebarButtonPressedBlendPercent);
    palette.buttonSelected = selectedBackground;
    palette.buttonGlyph = text;
    return palette;
}

int ResolveDashboardTitlebarCornerRadius(UINT dpi) {
    const UINT effectiveDpi = dpi == 0 ? kDefaultDpi : std::min(dpi, kMaxReasonableDpi);
    return std::max(1, MulDiv(kNativeTitlebarCornerRadiusLogical, static_cast<int>(effectiveDpi), kDefaultDpi));
}

int ResolveDashboardTitlebarResizeCornerHitSize(UINT dpi) {
    return std::max(1, ResolveDashboardTitlebarCornerRadius(dpi) - 1);
}

DashboardCloseButtonColors ResolveDashboardCloseButtonColors(HWND hwnd, bool pressed) {
    DashboardCloseButtonColors colors{
        pressed ? kDefaultCloseButtonPressedColor : kDefaultCloseButtonHoverColor, kDefaultCloseButtonGlyphColor};
    if (HighContrastEnabled()) {
        return DashboardCloseButtonColors{GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT)};
    }

    HTHEME theme = OpenThemeData(hwnd, kWindowThemeClassName);
    if (theme == nullptr) {
        return colors;
    }

    COLORREF themeBackground{};
    const int state = pressed ? CBS_PUSHED : CBS_HOT;
    if (SUCCEEDED(GetThemeColor(theme, WP_CLOSEBUTTON, state, TMT_FILLCOLOR, &themeBackground)) &&
        LooksLikeCloseActionColor(themeBackground)) {
        colors.background = themeBackground;
    }
    CloseThemeData(theme);
    return colors;
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
