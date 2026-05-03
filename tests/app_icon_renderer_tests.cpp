#include <cstdlib>
#include <gtest/gtest.h>

#include "config/config.h"
#include "widget/app_icon_geometry.h"

namespace {

AppConfig TestIconConfig() {
    AppConfig config;
    config.layout.colors.backgroundColor = ColorConfig::FromRgba(0x203040FFu);
    config.layout.colors.foregroundColor = ColorConfig::FromRgba(0xFFFFFFFFu);
    config.layout.colors.accentColor = ColorConfig::FromRgba(0x00BFFFFFu);
    config.layout.colors.mutedTextColor = ColorConfig::FromRgba(0x607080FFu);
    config.layout.colors.panelFillColor = ColorConfig::FromRgba(0x10203080u);
    return config;
}

std::uint8_t AlphaAt(const AppIconBitmap& bitmap, int x, int y) {
    return bitmap.bgra[(static_cast<size_t>(y) * static_cast<size_t>(bitmap.size) + static_cast<size_t>(x)) * 4u + 3u];
}

bool PixelIsNear(const AppIconBitmap& bitmap, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(bitmap.size) + static_cast<size_t>(x)) * 4u;
    const int db = std::abs(static_cast<int>(bitmap.bgra[offset + 0u]) - static_cast<int>(b));
    const int dg = std::abs(static_cast<int>(bitmap.bgra[offset + 1u]) - static_cast<int>(g));
    const int dr = std::abs(static_cast<int>(bitmap.bgra[offset + 2u]) - static_cast<int>(r));
    return dr <= 8 && dg <= 8 && db <= 8;
}

}  // namespace

TEST(AppIconRenderer, RendersRequestedSquareBitmap) {
    const AppIconBitmap bitmap = RenderAppIconBitmap(TestIconConfig(), 64);
    EXPECT_EQ(bitmap.size, 64);
    EXPECT_EQ(bitmap.bgra.size(), 64u * 64u * 4u);
}

TEST(AppIconRenderer, KeepsRoundedCornersTransparent) {
    const AppIconBitmap bitmap = RenderAppIconBitmap(TestIconConfig(), 256);
    EXPECT_EQ(AlphaAt(bitmap, 0, 0), 0);
    EXPECT_EQ(AlphaAt(bitmap, 255, 0), 0);
    EXPECT_EQ(AlphaAt(bitmap, 0, 255), 0);
    EXPECT_EQ(AlphaAt(bitmap, 255, 255), 0);
}

TEST(AppIconRenderer, UsesConfiguredForegroundAndAccentColors) {
    const AppIconBitmap bitmap = RenderAppIconBitmap(TestIconConfig(), 256);
    EXPECT_TRUE(PixelIsNear(bitmap, 100, 180, 0x00, 0xBF, 0xFF));
    EXPECT_TRUE(PixelIsNear(bitmap, 58, 146, 0x00, 0xBF, 0xFF));
    EXPECT_TRUE(PixelIsNear(bitmap, 132, 206, 0xFF, 0xFF, 0xFF));
}

TEST(AppIconRenderer, UsesCardBackgroundForIconBackground) {
    const AppIconBitmap bitmap = RenderAppIconBitmap(TestIconConfig(), 256);
    EXPECT_TRUE(PixelIsNear(bitmap, 128, 30, 0x18, 0x28, 0x38));
    EXPECT_EQ(AlphaAt(bitmap, 128, 30), 255);
    EXPECT_EQ(AlphaAt(bitmap, 128, 128), 255);
}

TEST(AppIconRenderer, ValidatesSupportedSizes) {
    EXPECT_FALSE(IsValidAppIconSize(15));
    EXPECT_TRUE(IsValidAppIconSize(16));
    EXPECT_TRUE(IsValidAppIconSize(1024));
    EXPECT_FALSE(IsValidAppIconSize(1025));
}
