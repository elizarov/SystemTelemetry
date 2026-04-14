#pragma once

#include <algorithm>
#include <cstdint>

#include <d2d1.h>

struct RenderPoint {
    int x = 0;
    int y = 0;

    D2D1_POINT_2F ToD2DPoint2F() const {
        return D2D1::Point2F(static_cast<float>(x), static_cast<float>(y));
    }
};

struct RenderRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int Width() const {
        return (std::max)(0, right - left);
    }

    int Height() const {
        return (std::max)(0, bottom - top);
    }

    bool IsEmpty() const {
        return right <= left || bottom <= top;
    }

    bool Contains(RenderPoint point) const {
        return point.x >= left && point.x < right && point.y >= top && point.y < bottom;
    }

    RenderRect Inflate(int dx, int dy) const {
        return RenderRect{left - dx, top - dy, right + dx, bottom + dy};
    }

    RenderPoint Center() const {
        return RenderPoint{left + (Width() / 2), top + (Height() / 2)};
    }

    D2D1_RECT_F ToD2DRectF() const {
        return D2D1::RectF(
            static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom));
    }
};

struct RenderColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    bool operator==(const RenderColor& other) const = default;

    RenderColor WithAlpha(std::uint8_t alpha) const {
        return RenderColor{r, g, b, alpha};
    }

    std::uint32_t PackedRgba() const {
        return (static_cast<std::uint32_t>(r) << 24) | (static_cast<std::uint32_t>(g) << 16) |
               (static_cast<std::uint32_t>(b) << 8) | static_cast<std::uint32_t>(a);
    }

    D2D1_COLOR_F ToD2DColorF() const {
        constexpr float kScale = 1.0f / 255.0f;
        return D2D1::ColorF(static_cast<float>(r) * kScale,
            static_cast<float>(g) * kScale,
            static_cast<float>(b) * kScale,
            static_cast<float>(a) * kScale);
    }
};

enum class StrokePattern {
    Solid,
    Dotted,
};

struct RenderStroke {
    RenderColor color{};
    float width = 1.0f;
    StrokePattern pattern = StrokePattern::Solid;

    static RenderStroke Solid(RenderColor color, float width = 1.0f) {
        return RenderStroke{color, width, StrokePattern::Solid};
    }

    static RenderStroke Dotted(RenderColor color, float width = 1.0f) {
        return RenderStroke{color, width, StrokePattern::Dotted};
    }
};

enum class TextStyleId {
    Title,
    Big,
    Value,
    Label,
    Text,
    Small,
    Footer,
    ClockTime,
    ClockDate,
};

enum class TextHorizontalAlign {
    Leading,
    Center,
    Trailing,
};

enum class TextVerticalAlign {
    Top,
    Center,
    Bottom,
};

struct TextLayoutOptions {
    TextHorizontalAlign horizontalAlign = TextHorizontalAlign::Leading;
    TextVerticalAlign verticalAlign = TextVerticalAlign::Top;
    bool wrap = true;
    bool ellipsis = false;
    bool clip = true;

    static TextLayoutOptions SingleLine(TextHorizontalAlign horizontal = TextHorizontalAlign::Leading,
        TextVerticalAlign vertical = TextVerticalAlign::Center,
        bool clip = true,
        bool ellipsis = false) {
        return TextLayoutOptions{horizontal, vertical, false, ellipsis, clip};
    }

    static TextLayoutOptions Wrapped(TextHorizontalAlign horizontal = TextHorizontalAlign::Leading,
        TextVerticalAlign vertical = TextVerticalAlign::Top,
        bool clip = true,
        bool ellipsis = false) {
        return TextLayoutOptions{horizontal, vertical, true, ellipsis, clip};
    }
};
