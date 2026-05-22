#include "layout_edit_dialog/theme_preview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "config/color_math.h"
#include "config/config.h"

namespace {

COLORREF ColorConfigToColorRef(const ColorConfig& color) {
    const unsigned int rgba = color.ToRgba();
    return RGB((rgba >> 24) & 0xFFu, (rgba >> 16) & 0xFFu, (rgba >> 8) & 0xFFu);
}

uint32_t ColorRefToDibPixel(COLORREF color) {
    return static_cast<uint32_t>(GetBValue(color)) | (static_cast<uint32_t>(GetGValue(color)) << 8u) |
           (static_cast<uint32_t>(GetRValue(color)) << 16u);
}

struct ThemeTriangleGeometry {
    int side = 1;
    int height = 1;
    int topLabelBand = 0;
    int bottomLabelBand = 0;
    double leftX = 0.0;
    double rightX = 0.0;
    double topY = 0.0;
    double bottomX = 0.0;
    double bottomY = 0.0;
};

struct ThemePreviewPixelCacheKey {
    int width = 0;
    int height = 0;
    int dpi = 0;
    uint32_t systemFace = 0;
    unsigned int background = 0;
    unsigned int foreground = 0;
    unsigned int accent = 0;

    bool operator==(const ThemePreviewPixelCacheKey& other) const {
        return width == other.width && height == other.height && dpi == other.dpi && systemFace == other.systemFace &&
               background == other.background && foreground == other.foreground && accent == other.accent;
    }
};

struct ThemePreviewPixelCacheEntry {
    ThemePreviewPixelCacheKey key;
    std::vector<uint32_t> pixels;
};

constexpr size_t kThemePreviewPixelCacheLimit = 24;

int ScaledPreviewPixels(int dpi, int logicalPixels) {
    return MulDiv(logicalPixels, std::max(1, dpi), USER_DEFAULT_SCREEN_DPI);
}

ThemeTriangleGeometry BuildThemeTriangleGeometry(const RECT& rect, int dpi) {
    const int rectWidth = std::max(1, static_cast<int>(rect.right - rect.left));
    const int rectHeight = std::max(1, static_cast<int>(rect.bottom - rect.top));
    const int topLabelBand = rectHeight >= 54 ? ScaledPreviewPixels(dpi, 24) : 0;
    const int bottomLabelBand = rectHeight >= 54 ? ScaledPreviewPixels(dpi, 22) : 0;
    const int availableWidth = std::max(1, rectWidth - 2);
    const int availableHeight = std::max(1, rectHeight - 2 - topLabelBand - bottomLabelBand);
    const int side = std::max(1, std::min(availableWidth, static_cast<int>(availableHeight * 2.0 / std::sqrt(3.0))));
    const int triangleHeight = std::max(1, static_cast<int>(std::lround(side * std::sqrt(3.0) / 2.0)));
    const double leftX = (rectWidth - side) / 2.0;
    const double rightX = leftX + side;
    const double topY = topLabelBand + (availableHeight - triangleHeight) / 2.0;
    const double bottomX = (leftX + rightX) / 2.0;
    const double bottomY = topY + triangleHeight;
    return {side, triangleHeight, topLabelBand, bottomLabelBand, leftX, rightX, topY, bottomX, bottomY};
}

void DrawThemePreviewLabel(HDC dc, std::string_view text, const RECT& rect, UINT format) {
    RECT labelRect = rect;
    DrawTextA(dc, text.data(), static_cast<int>(text.size()), &labelRect, format | DT_SINGLELINE | DT_NOPREFIX);
}

int DeviceDpiY(HDC dc) {
    return dc != nullptr ? std::max(1, GetDeviceCaps(dc, LOGPIXELSY)) : USER_DEFAULT_SCREEN_DPI;
}

void FillThemePreviewPixels(
    std::vector<uint32_t>& pixels, int width, int height, int dpi, uint32_t backgroundFill, const ThemeConfig& theme) {
    std::fill(pixels.begin(), pixels.end(), backgroundFill);

    RECT localRect{0, 0, width, height};
    const ThemeTriangleGeometry geometry = BuildThemeTriangleGeometry(localRect, dpi);
    const OklabColor backgroundLab = OklabFromColorBytes(ColorBytesFromRgba(theme.background.ToRgba()));
    const OklabColor foregroundLab = OklabFromColorBytes(ColorBytesFromRgba(theme.foreground.ToRgba()));
    const OklabColor accentLab = OklabFromColorBytes(ColorBytesFromRgba(theme.accent.ToRgba()));
    const double denom = (geometry.topY - geometry.bottomY) * (geometry.leftX - geometry.bottomX) +
                         (geometry.bottomX - geometry.rightX) * (geometry.topY - geometry.bottomY);
    if (std::abs(denom) < 0.0001) {
        return;
    }

    const int minX = std::clamp(static_cast<int>(std::floor(geometry.leftX)), 0, width - 1);
    const int maxX = std::clamp(static_cast<int>(std::ceil(geometry.rightX)), 0, width - 1);
    const int minY = std::clamp(static_cast<int>(std::floor(geometry.topY)), 0, height - 1);
    const int maxY = std::clamp(static_cast<int>(std::ceil(geometry.bottomY)), 0, height - 1);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const double sampleX = x + 0.5;
            const double sampleY = y + 0.5;
            const double backgroundWeight = ((geometry.topY - geometry.bottomY) * (sampleX - geometry.bottomX) +
                                                (geometry.bottomX - geometry.rightX) * (sampleY - geometry.bottomY)) /
                                            denom;
            const double foregroundWeight = ((geometry.bottomY - geometry.topY) * (sampleX - geometry.bottomX) +
                                                (geometry.leftX - geometry.bottomX) * (sampleY - geometry.bottomY)) /
                                            denom;
            const double accentWeight = 1.0 - backgroundWeight - foregroundWeight;
            if (backgroundWeight < -0.001 || foregroundWeight < -0.001 || accentWeight < -0.001) {
                continue;
            }
            const OklabColor mixedLab{
                backgroundLab.l * backgroundWeight + foregroundLab.l * foregroundWeight + accentLab.l * accentWeight,
                backgroundLab.a * backgroundWeight + foregroundLab.a * foregroundWeight + accentLab.a * accentWeight,
                backgroundLab.b * backgroundWeight + foregroundLab.b * foregroundWeight + accentLab.b * accentWeight};
            const ColorBytes mixed = ColorBytesFromOklab(mixedLab, 255.0);
            const int red = std::clamp(static_cast<int>(std::lround(mixed.r)), 0, 255);
            const int green = std::clamp(static_cast<int>(std::lround(mixed.g)), 0, 255);
            const int blue = std::clamp(static_cast<int>(std::lround(mixed.b)), 0, 255);
            pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                static_cast<uint32_t>(blue) | (static_cast<uint32_t>(green) << 8u) |
                (static_cast<uint32_t>(red) << 16u);
        }
    }
}

std::vector<ThemePreviewPixelCacheEntry>& ThemePreviewPixelCache() {
    thread_local std::vector<ThemePreviewPixelCacheEntry> cache;
    return cache;
}

const std::vector<uint32_t>& CachedThemePreviewPixels(
    int width, int height, int dpi, uint32_t systemFace, const ThemeConfig& theme) {
    ThemePreviewPixelCacheKey key{
        width, height, dpi, systemFace, theme.background.ToRgba(), theme.foreground.ToRgba(), theme.accent.ToRgba()};
    std::vector<ThemePreviewPixelCacheEntry>& cache = ThemePreviewPixelCache();
    auto found = std::find_if(
        cache.begin(), cache.end(), [&](const ThemePreviewPixelCacheEntry& entry) { return entry.key == key; });
    if (found != cache.end()) {
        if (found != cache.begin()) {
            ThemePreviewPixelCacheEntry entry = std::move(*found);
            cache.erase(found);
            cache.insert(cache.begin(), std::move(entry));
        }
        return cache.front().pixels;
    }

    ThemePreviewPixelCacheEntry entry;
    entry.key = key;
    entry.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    FillThemePreviewPixels(entry.pixels, width, height, dpi, systemFace, theme);
    cache.insert(cache.begin(), std::move(entry));
    if (cache.size() > kThemePreviewPixelCacheLimit) {
        cache.pop_back();
    }
    return cache.front().pixels;
}

}  // namespace

const ThemeConfig* FindActiveThemeConfig(const AppConfig& config) {
    for (const ThemeConfig& theme : config.layout.themes) {
        if (theme.name == config.display.theme) {
            return &theme;
        }
    }
    return nullptr;
}

void DrawThemePreviewTriangle(HDC dc, const RECT& rect, const ThemeConfig& theme) {
    if (dc == nullptr) {
        return;
    }

    const int width = std::max(1, static_cast<int>(rect.right - rect.left));
    const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
    const int dpi = DeviceDpiY(dc);
    const uint32_t systemFace = ColorRefToDibPixel(GetSysColor(COLOR_3DFACE));
    const std::vector<uint32_t>& pixels = CachedThemePreviewPixels(width, height, dpi, systemFace, theme);

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc,
        rect.left,
        rect.top,
        static_cast<DWORD>(width),
        static_cast<DWORD>(height),
        0,
        0,
        0,
        static_cast<UINT>(height),
        pixels.data(),
        &bitmapInfo,
        DIB_RGB_COLORS);

    const ThemeTriangleGeometry geometry = BuildThemeTriangleGeometry(RECT{0, 0, width, height}, dpi);
    HPEN outlinePen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWTEXT));
    HPEN guidePen = CreatePen(PS_SOLID, 1, ColorConfigToColorRef(theme.guide));
    HGDIOBJ oldPen = SelectObject(dc, outlinePen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    POINT points[] = {{rect.left + static_cast<LONG>(std::lround(geometry.leftX)),
                          rect.top + static_cast<LONG>(std::lround(geometry.topY))},
        {rect.left + static_cast<LONG>(std::lround(geometry.rightX)),
            rect.top + static_cast<LONG>(std::lround(geometry.topY))},
        {rect.left + static_cast<LONG>(std::lround(geometry.bottomX)),
            rect.top + static_cast<LONG>(std::lround(geometry.bottomY))}};
    Polygon(dc, points, 3);
    SelectObject(dc, guidePen);
    MoveToEx(dc,
        rect.left + static_cast<int>(std::lround(geometry.bottomX)),
        rect.top + static_cast<int>(std::lround(geometry.topY)),
        nullptr);
    LineTo(dc,
        rect.left + static_cast<int>(std::lround(geometry.bottomX)),
        rect.top + static_cast<int>(std::lround(geometry.bottomY)));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(guidePen);
    DeleteObject(outlinePen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT)));
    const int labelInset = ScaledPreviewPixels(dpi, 2);
    const int labelHeight = std::max(1, geometry.topLabelBand - (labelInset * 2));
    const RECT topLeftLabel{rect.left,
        rect.top + labelInset,
        rect.left + static_cast<LONG>(std::lround(geometry.bottomX)) - 4,
        rect.top + labelInset + labelHeight};
    const RECT topRightLabel{rect.left + static_cast<LONG>(std::lround(geometry.bottomX)) + 4,
        rect.top + labelInset,
        rect.right,
        rect.top + labelInset + labelHeight};
    const RECT bottomLabel{rect.left,
        rect.top + static_cast<LONG>(std::lround(geometry.bottomY)) + labelInset,
        rect.right,
        rect.bottom - labelInset};
    DrawThemePreviewLabel(dc, "background", topLeftLabel, DT_LEFT | DT_TOP | DT_END_ELLIPSIS);
    DrawThemePreviewLabel(dc, "foreground", topRightLabel, DT_RIGHT | DT_TOP | DT_END_ELLIPSIS);
    DrawThemePreviewLabel(dc, "accent", bottomLabel, DT_CENTER | DT_TOP | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}
