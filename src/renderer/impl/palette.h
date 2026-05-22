#pragma once

#include <array>
#include <cstdint>
#include <d2d1.h>

#include "renderer/render_types.h"

struct ColorsConfig;
struct LayoutGuideSheetConfig;

struct RenderColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    bool operator==(const RenderColor& other) const = default;

    D2D1_COLOR_F ToD2DColorF() const;
};

class RendererPalette {
public:
    RendererPalette();
    explicit RendererPalette(const ColorsConfig& colors, const LayoutGuideSheetConfig& layoutGuideSheet);

    void Rebuild(const ColorsConfig& colors, const LayoutGuideSheetConfig& layoutGuideSheet);
    const RenderColor& Get(RenderColorId id) const;

private:
    static constexpr std::size_t kColorCount = static_cast<std::size_t>(RenderColorId::Count);

    std::array<RenderColor, kColorCount> colors_{};
};
