#include "dashboard/display_placement_menu_bitmap.h"

#include <algorithm>

#include "dashboard/native_theme_colors.h"
#include "util/scale.h"

namespace {

int RectWidth(const RECT& rect) {
    return rect.right - rect.left;
}

int RectHeight(const RECT& rect) {
    return rect.bottom - rect.top;
}

int SystemMetricForDpi(int metric, UINT dpi) {
    using GetSystemMetricsForDpiFn = int(WINAPI*)(int, UINT);
    static const auto getSystemMetricsForDpi = reinterpret_cast<GetSystemMetricsForDpiFn>(
        GetProcAddress(GetModuleHandleA("user32.dll"), "GetSystemMetricsForDpi"));
    return getSystemMetricsForDpi != nullptr ? getSystemMetricsForDpi(metric, dpi) : GetSystemMetrics(metric);
}

bool QueryNonClientMetricsForDpi(NONCLIENTMETRICSA& metrics, UINT dpi) {
    using SystemParametersInfoForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    static const auto systemParametersInfoForDpi = reinterpret_cast<SystemParametersInfoForDpiFn>(
        GetProcAddress(GetModuleHandleA("user32.dll"), "SystemParametersInfoForDpi"));
    metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (systemParametersInfoForDpi != nullptr &&
        systemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
        return true;
    }
    return SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE;
}

int NativeMenuFontHeight(UINT dpi) {
    NONCLIENTMETRICSA metrics{};
    if (!QueryNonClientMetricsForDpi(metrics, dpi)) {
        return 0;
    }
    HFONT font = CreateFontIndirectA(&metrics.lfMenuFont);
    if (font == nullptr) {
        return 0;
    }

    int height = 0;
    HDC hdc = GetDC(nullptr);
    if (hdc != nullptr) {
        HGDIOBJ oldFont = SelectObject(hdc, font);
        TEXTMETRICA textMetrics{};
        if (GetTextMetricsA(hdc, &textMetrics)) {
            height = textMetrics.tmHeight + textMetrics.tmExternalLeading;
        }
        SelectObject(hdc, oldFont);
        ReleaseDC(nullptr, hdc);
    }
    DeleteObject(font);
    return height;
}

int ResolveNativePopupMenuRowHeight(UINT dpi) {
    const int fontHeight = NativeMenuFontHeight(dpi);
    const int textRowHeight = fontHeight > 0 ? fontHeight + ScaleLogicalToPhysical(8, dpi) : 0;
    return std::max({SystemMetricForDpi(SM_CYMENU, dpi), SystemMetricForDpi(SM_CYMENUCHECK, dpi), textRowHeight});
}

void PaintBitmapRect(DisplayPlacementMenuBitmapPixel* pixels, int width, int height, RECT rect, COLORREF color) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return;
    }
    rect.left = std::clamp<LONG>(rect.left, 0, width);
    rect.top = std::clamp<LONG>(rect.top, 0, height);
    rect.right = std::clamp<LONG>(rect.right, 0, width);
    rect.bottom = std::clamp<LONG>(rect.bottom, 0, height);
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }

    const DisplayPlacementMenuBitmapPixel pixel = OpaqueDisplayPlacementMenuBitmapPixel(color);
    for (int y = rect.top; y < rect.bottom; ++y) {
        DisplayPlacementMenuBitmapPixel* row = pixels + y * width;
        for (int x = rect.left; x < rect.right; ++x) {
            row[x] = pixel;
        }
    }
}

void PaintBitmapRectOutline(
    DisplayPlacementMenuBitmapPixel* pixels, int width, int height, const RECT& rect, int thickness, COLORREF color) {
    const int lineThickness = std::max(1, thickness);
    PaintBitmapRect(pixels, width, height, RECT{rect.left, rect.top, rect.right, rect.top + lineThickness}, color);
    PaintBitmapRect(
        pixels, width, height, RECT{rect.left, rect.bottom - lineThickness, rect.right, rect.bottom}, color);
    PaintBitmapRect(pixels, width, height, RECT{rect.left, rect.top, rect.left + lineThickness, rect.bottom}, color);
    PaintBitmapRect(pixels, width, height, RECT{rect.right - lineThickness, rect.top, rect.right, rect.bottom}, color);
}

COLORREF ResolveDisplayPlacementMenuBitmapBackground(COLORREF menuColor, COLORREF highlightColor, bool active) {
    return active ? ResolveNativeThemeSelectedBackground(menuColor, highlightColor) : menuColor;
}

}  // namespace

DisplayPlacementMenuBitmapPixel OpaqueDisplayPlacementMenuBitmapPixel(COLORREF color) {
    return DisplayPlacementMenuBitmapPixel{GetBValue(color), GetGValue(color), GetRValue(color), 255};
}

int ResolveNativeMenuBitmapSize(UINT dpi) {
    const int rowHeight = ResolveNativePopupMenuRowHeight(dpi);
    const int smallIcon = std::min(SystemMetricForDpi(SM_CXSMICON, dpi), SystemMetricForDpi(SM_CYSMICON, dpi));
    const int checkBitmap = std::min(SystemMetricForDpi(SM_CXMENUCHECK, dpi), SystemMetricForDpi(SM_CYMENUCHECK, dpi));
    const int preferred = std::max(smallIcon, checkBitmap);
    const int verticalInset = std::max(2, ScaleLogicalToPhysical(3, dpi));
    return std::max(1, std::min(preferred, std::max(1, rowHeight - verticalInset)));
}

void PaintDisplayPlacementMenuBitmapPixels(DisplayPlacementMenuBitmapPixel* pixels,
    int bitmapSize,
    const DisplayMenuOption& option,
    COLORREF menuColor,
    COLORREF menuTextColor,
    COLORREF highlightColor) {
    if (pixels == nullptr || bitmapSize <= 0) {
        return;
    }

    const COLORREF backgroundColor =
        ResolveDisplayPlacementMenuBitmapBackground(menuColor, highlightColor, option.matchesCommittedConfig);
    std::fill(pixels, pixels + bitmapSize * bitmapSize, OpaqueDisplayPlacementMenuBitmapPixel(backgroundColor));

    const int padding = std::max(1, bitmapSize / 8);
    const RECT bounds{padding, padding, bitmapSize - padding, bitmapSize - padding};
    const DisplayPlacementSchematicGeometry geometry = ComputeDisplayPlacementSchematicGeometry(option, bounds);
    if (geometry.displayRect.right <= geometry.displayRect.left ||
        geometry.displayRect.bottom <= geometry.displayRect.top) {
        return;
    }

    const COLORREF fillColor = BlendNativeThemeColor(highlightColor, backgroundColor, 32);
    const COLORREF outlineColor = BlendNativeThemeColor(menuTextColor, backgroundColor, 75);
    const COLORREF dividerColor = BlendNativeThemeColor(menuTextColor, backgroundColor, 82);
    PaintBitmapRect(pixels, bitmapSize, bitmapSize, geometry.caseDashRect, fillColor);
    if (geometry.hasDivider) {
        PaintBitmapRect(pixels, bitmapSize, bitmapSize, geometry.dividerRect, dividerColor);
    }
    PaintBitmapRectOutline(
        pixels, bitmapSize, bitmapSize, geometry.displayRect, std::max(1, bitmapSize / 14), outlineColor);
}

HBITMAP CreateDisplayPlacementMenuBitmap(const DisplayMenuOption& option, UINT dpi) {
    const int bitmapSize = ResolveNativeMenuBitmapSize(dpi);
    if (bitmapSize <= 0) {
        return nullptr;
    }

    BITMAPINFO header{};
    header.bmiHeader.biSize = sizeof(header.bmiHeader);
    header.bmiHeader.biWidth = bitmapSize;
    header.bmiHeader.biHeight = -bitmapSize;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &header, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        return nullptr;
    }

    auto* pixels = static_cast<DisplayPlacementMenuBitmapPixel*>(bits);
    PaintDisplayPlacementMenuBitmapPixels(
        pixels, bitmapSize, option, GetSysColor(COLOR_MENU), GetSysColor(COLOR_MENUTEXT), GetSysColor(COLOR_HIGHLIGHT));
    return bitmap;
}
