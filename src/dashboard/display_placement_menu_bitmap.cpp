#include "dashboard/display_placement_menu_bitmap.h"

#include <algorithm>
#include <cmath>

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

void PaintBitmapLine(DisplayPlacementMenuBitmapPixel* pixels,
    int width,
    int height,
    POINT from,
    POINT to,
    int thickness,
    COLORREF color) {
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    const int steps = std::max(std::abs(dx), std::abs(dy));
    if (steps <= 0) {
        PaintBitmapRect(pixels, width, height, RECT{from.x, from.y, from.x + 1, from.y + 1}, color);
        return;
    }

    const int radius = std::max(0, thickness / 2);
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const int x = static_cast<int>(std::lround(static_cast<double>(from.x) + static_cast<double>(dx) * t));
        const int y = static_cast<int>(std::lround(static_cast<double>(from.y) + static_cast<double>(dy) * t));
        PaintBitmapRect(pixels, width, height, RECT{x - radius, y - radius, x + radius + 1, y + radius + 1}, color);
    }
}

void PaintActiveBadge(DisplayPlacementMenuBitmapPixel* pixels,
    int bitmapSize,
    COLORREF menuColor,
    COLORREF menuTextColor,
    COLORREF highlightColor,
    COLORREF highlightTextColor) {
    const int badgeSize = std::clamp(bitmapSize / 2, 7, 12);
    const int inset = std::max(1, bitmapSize / 16);
    const RECT badge{inset, inset, inset + badgeSize, inset + badgeSize};
    const COLORREF fillColor = BlendDisplayPlacementMenuBitmapColor(highlightColor, menuColor, 88);
    const COLORREF borderColor = BlendDisplayPlacementMenuBitmapColor(menuTextColor, fillColor, 40);
    PaintBitmapRect(pixels, bitmapSize, bitmapSize, badge, fillColor);
    PaintBitmapRectOutline(pixels, bitmapSize, bitmapSize, badge, 1, borderColor);

    const int left = badge.left + std::max(2, badgeSize / 5);
    const int middleX = badge.left + std::max(3, (badgeSize * 2) / 5);
    const int right = badge.right - std::max(2, badgeSize / 5);
    const int middleY = badge.bottom - std::max(3, badgeSize / 3);
    const int leftY = badge.top + std::max(3, badgeSize / 2);
    const int rightY = badge.top + std::max(2, badgeSize / 4);
    const int thickness = std::max(1, badgeSize / 5);
    PaintBitmapLine(
        pixels, bitmapSize, bitmapSize, POINT{left, leftY}, POINT{middleX, middleY}, thickness, highlightTextColor);
    PaintBitmapLine(
        pixels, bitmapSize, bitmapSize, POINT{middleX, middleY}, POINT{right, rightY}, thickness, highlightTextColor);
}

}  // namespace

COLORREF BlendDisplayPlacementMenuBitmapColor(COLORREF foreground, COLORREF background, int foregroundPercent) {
    const int clampedPercent = std::clamp(foregroundPercent, 0, 100);
    const int backgroundPercent = 100 - clampedPercent;
    const auto blendChannel = [&](int foregroundChannel, int backgroundChannel) {
        return static_cast<BYTE>((foregroundChannel * clampedPercent + backgroundChannel * backgroundPercent) / 100);
    };
    return RGB(blendChannel(GetRValue(foreground), GetRValue(background)),
        blendChannel(GetGValue(foreground), GetGValue(background)),
        blendChannel(GetBValue(foreground), GetBValue(background)));
}

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
    COLORREF highlightColor,
    COLORREF highlightTextColor) {
    if (pixels == nullptr || bitmapSize <= 0) {
        return;
    }

    std::fill(pixels, pixels + bitmapSize * bitmapSize, OpaqueDisplayPlacementMenuBitmapPixel(menuColor));

    const int padding = std::max(1, bitmapSize / 8);
    const RECT bounds{padding, padding, bitmapSize - padding, bitmapSize - padding};
    const DisplayPlacementSchematicGeometry geometry = ComputeDisplayPlacementSchematicGeometry(option, bounds);
    if (geometry.displayRect.right <= geometry.displayRect.left ||
        geometry.displayRect.bottom <= geometry.displayRect.top) {
        return;
    }

    const COLORREF fillColor = BlendDisplayPlacementMenuBitmapColor(highlightColor, menuColor, 32);
    const COLORREF outlineColor = BlendDisplayPlacementMenuBitmapColor(menuTextColor, menuColor, 75);
    const COLORREF dividerColor = BlendDisplayPlacementMenuBitmapColor(menuTextColor, menuColor, 82);
    PaintBitmapRect(pixels, bitmapSize, bitmapSize, geometry.caseDashRect, fillColor);
    if (geometry.hasDivider) {
        PaintBitmapRect(pixels, bitmapSize, bitmapSize, geometry.dividerRect, dividerColor);
    }
    PaintBitmapRectOutline(
        pixels, bitmapSize, bitmapSize, geometry.displayRect, std::max(1, bitmapSize / 14), outlineColor);
    if (option.matchesCommittedConfig) {
        PaintActiveBadge(pixels, bitmapSize, menuColor, menuTextColor, highlightColor, highlightTextColor);
    }
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
    PaintDisplayPlacementMenuBitmapPixels(pixels,
        bitmapSize,
        option,
        GetSysColor(COLOR_MENU),
        GetSysColor(COLOR_MENUTEXT),
        GetSysColor(COLOR_HIGHLIGHT),
        GetSysColor(COLOR_HIGHLIGHTTEXT));
    return bitmap;
}
