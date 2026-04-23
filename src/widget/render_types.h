#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

struct RenderPoint {
    int x = 0;
    int y = 0;
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
};

enum class RenderColorId {
    Background,
    Foreground,
    Icon,
    Accent,
    PeakGhost,
    MutedText,
    Track,
    LayoutGuide,
    ActiveEdit,
    PanelBorder,
    PanelFill,
    GraphBackground,
    GraphMarker,
    GraphAxis,
    Count,
};

enum class StrokePattern {
    Solid,
    Dotted,
};

enum class LayoutSimilarityIndicatorMode {
    ActiveGuide,
    AllHorizontal,
    AllVertical,
};

struct RenderStroke {
    RenderColorId color = RenderColorId::Foreground;
    float width = 1.0f;
    StrokePattern pattern = StrokePattern::Solid;

    static RenderStroke Solid(RenderColorId color, float width = 1.0f);

    static RenderStroke Dotted(RenderColorId color, float width = 1.0f);
};

struct RenderArc {
    RenderPoint center{};
    int radiusX = 0;
    int radiusY = 0;
    double startAngleDegrees = 0.0;
    double sweepAngleDegrees = 0.0;
};

enum class RenderPathCommandType {
    MoveTo,
    LineTo,
    ArcTo,
    Close,
};

struct RenderPathArc {
    RenderPoint center{};
    int radiusX = 0;
    int radiusY = 0;
    double startAngleDegrees = 0.0;
    double sweepAngleDegrees = 0.0;
};

struct RenderPathCommand {
    RenderPathCommandType type = RenderPathCommandType::MoveTo;
    RenderPoint point{};
    RenderPathArc arc{};
};

struct RenderPath {
    bool IsEmpty() const;

    std::span<const RenderPathCommand> Commands() const;

    void MoveTo(RenderPoint point);

    void LineTo(RenderPoint point);

    void ArcTo(RenderPoint center, int radiusX, int radiusY, double startAngleDegrees, double sweepAngleDegrees);

    void Close();

private:
    static constexpr size_t InlineCommandCapacity = 8;

    void PushCommand(const RenderPathCommand& command);

    std::array<RenderPathCommand, InlineCommandCapacity> inlineCommands_{};
    std::vector<RenderPathCommand> overflowCommands_;
    size_t commandCount_ = 0;
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
