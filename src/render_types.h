#pragma once

#include <cstdint>

#include <d2d1.h>

struct RenderPoint {
    int x = 0;
    int y = 0;

    D2D1_POINT_2F ToD2DPoint2F() const;
};

struct RenderRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int Width() const;

    int Height() const;

    bool IsEmpty() const;

    bool Contains(RenderPoint point) const;

    RenderRect Inflate(int dx, int dy) const;

    RenderPoint Center() const;

    D2D1_RECT_F ToD2DRectF() const;
};

struct RenderColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    bool operator==(const RenderColor& other) const = default;

    RenderColor WithAlpha(std::uint8_t alpha) const;

    std::uint32_t PackedRgba() const;

    D2D1_COLOR_F ToD2DColorF() const;
};

enum class StrokePattern {
    Solid,
    Dotted,
};

struct RenderStroke {
    RenderColor color{};
    float width = 1.0f;
    StrokePattern pattern = StrokePattern::Solid;

    static RenderStroke Solid(RenderColor color, float width = 1.0f);

    static RenderStroke Dotted(RenderColor color, float width = 1.0f);
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
        bool ellipsis = false);

    static TextLayoutOptions Wrapped(TextHorizontalAlign horizontal = TextHorizontalAlign::Leading,
        TextVerticalAlign vertical = TextVerticalAlign::Top,
        bool clip = true,
        bool ellipsis = false);
};
