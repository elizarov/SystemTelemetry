#include "dashboard/native_theme_colors.h"

#include <algorithm>

namespace {

int ColorChannel(COLORREF color, int shift) {
    return static_cast<int>((color >> shift) & 0xFF);
}

}  // namespace

COLORREF BlendNativeThemeColor(COLORREF foreground, COLORREF background, int foregroundPercent) {
    const int clampedPercent = std::clamp(foregroundPercent, 0, 100);
    const int backgroundPercent = 100 - clampedPercent;
    const auto blendChannel = [&](int foregroundChannel, int backgroundChannel) {
        return static_cast<BYTE>((foregroundChannel * clampedPercent + backgroundChannel * backgroundPercent) / 100);
    };
    return RGB(blendChannel(ColorChannel(foreground, 0), ColorChannel(background, 0)),
        blendChannel(ColorChannel(foreground, 8), ColorChannel(background, 8)),
        blendChannel(ColorChannel(foreground, 16), ColorChannel(background, 16)));
}

COLORREF ResolveNativeThemeSelectedBackground(COLORREF background, COLORREF highlight) {
    return BlendNativeThemeColor(highlight, background, 22);
}
