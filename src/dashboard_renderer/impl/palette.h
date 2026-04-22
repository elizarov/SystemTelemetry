#pragma once

#include <array>
#include <cstdint>
#include <d2d1.h>

#include "config/config.h"
#include "dashboard_renderer/render_types.h"

struct RenderColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    bool operator==(const RenderColor& other) const = default;

    std::uint32_t PackedRgba() const;
    D2D1_COLOR_F ToD2DColorF() const;
};

class DashboardPalette {
public:
    DashboardPalette();
    explicit DashboardPalette(const ColorsConfig& config);

    void Rebuild(const ColorsConfig& config);
    const RenderColor& Get(RenderColorId id) const;

private:
    static constexpr std::size_t kColorCount = static_cast<std::size_t>(RenderColorId::Count);

    std::array<RenderColor, kColorCount> colors_{};
};
