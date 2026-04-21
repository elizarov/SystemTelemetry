#include "dashboard_renderer/palette.h"

namespace {

RenderColor ToRenderColor(ColorConfig color) {
    const unsigned int rgb = color.ToRgb();
    return RenderColor{static_cast<std::uint8_t>((rgb >> 16) & 0xFFu),
        static_cast<std::uint8_t>((rgb >> 8) & 0xFFu),
        static_cast<std::uint8_t>(rgb & 0xFFu),
        color.Alpha()};
}

std::size_t ColorSlot(RenderColorId id) {
    return static_cast<std::size_t>(id);
}

}  // namespace

std::uint32_t RenderColor::PackedRgba() const {
    return (static_cast<std::uint32_t>(r) << 24) | (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) << 8) | static_cast<std::uint32_t>(a);
}

D2D1_COLOR_F RenderColor::ToD2DColorF() const {
    constexpr float kScale = 1.0f / 255.0f;
    return D2D1::ColorF(static_cast<float>(r) * kScale,
        static_cast<float>(g) * kScale,
        static_cast<float>(b) * kScale,
        static_cast<float>(a) * kScale);
}

DashboardPalette::DashboardPalette() = default;

DashboardPalette::DashboardPalette(const ColorsConfig& config) {
    Rebuild(config);
}

void DashboardPalette::Rebuild(const ColorsConfig& config) {
    colors_[ColorSlot(RenderColorId::Background)] = ToRenderColor(config.backgroundColor);
    colors_[ColorSlot(RenderColorId::Foreground)] = ToRenderColor(config.foregroundColor);
    colors_[ColorSlot(RenderColorId::Icon)] = ToRenderColor(config.iconColor);
    colors_[ColorSlot(RenderColorId::Accent)] = ToRenderColor(config.accentColor);
    colors_[ColorSlot(RenderColorId::PeakGhost)] = ToRenderColor(config.peakGhostColor);
    colors_[ColorSlot(RenderColorId::MutedText)] = ToRenderColor(config.mutedTextColor);
    colors_[ColorSlot(RenderColorId::Track)] = ToRenderColor(config.trackColor);
    colors_[ColorSlot(RenderColorId::LayoutGuide)] = ToRenderColor(config.layoutGuideColor);
    colors_[ColorSlot(RenderColorId::ActiveEdit)] = ToRenderColor(config.activeEditColor);
    colors_[ColorSlot(RenderColorId::PanelBorder)] = ToRenderColor(config.panelBorderColor);
    colors_[ColorSlot(RenderColorId::PanelFill)] = ToRenderColor(config.panelFillColor);
    colors_[ColorSlot(RenderColorId::GraphBackground)] = ToRenderColor(config.graphBackgroundColor);
    colors_[ColorSlot(RenderColorId::GraphMarker)] = ToRenderColor(config.graphMarkerColor);
    colors_[ColorSlot(RenderColorId::GraphAxis)] = ToRenderColor(config.graphAxisColor);
}

const RenderColor& DashboardPalette::Get(RenderColorId id) const {
    return colors_[ColorSlot(id)];
}
