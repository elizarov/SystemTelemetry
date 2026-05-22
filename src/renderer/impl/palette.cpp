#include "renderer/impl/palette.h"

#include "config/config.h"

namespace {

RenderColor ToRenderColor(ColorConfig color) {
    const unsigned int rgb = color.ToRgb();
    return RenderColor{
        static_cast<std::uint8_t>((rgb >> 16) & 0xFFu),
        static_cast<std::uint8_t>((rgb >> 8) & 0xFFu),
        static_cast<std::uint8_t>(rgb & 0xFFu),
        color.Alpha()};
}

std::size_t ColorSlot(RenderColorId id) {
    return static_cast<std::size_t>(id);
}

}  // namespace

D2D1_COLOR_F RenderColor::ToD2DColorF() const {
    constexpr float kScale = 1.0f / 255.0f;
    return D2D1::ColorF(
        static_cast<float>(r) * kScale,
        static_cast<float>(g) * kScale,
        static_cast<float>(b) * kScale,
        static_cast<float>(a) * kScale);
}

RendererPalette::RendererPalette() = default;

RendererPalette::RendererPalette(const ColorsConfig& colors, const LayoutGuideSheetConfig& layoutGuideSheet) {
    Rebuild(colors, layoutGuideSheet);
}

void RendererPalette::Rebuild(const ColorsConfig& colors, const LayoutGuideSheetConfig& layoutGuideSheet) {
    colors_[ColorSlot(RenderColorId::Background)] = ToRenderColor(colors.backgroundColor);
    colors_[ColorSlot(RenderColorId::Foreground)] = ToRenderColor(colors.foregroundColor);
    colors_[ColorSlot(RenderColorId::Icon)] = ToRenderColor(colors.iconColor);
    colors_[ColorSlot(RenderColorId::Accent)] = ToRenderColor(colors.accentColor);
    colors_[ColorSlot(RenderColorId::PeakGhost)] = ToRenderColor(colors.peakGhostColor);
    colors_[ColorSlot(RenderColorId::Warning)] = ToRenderColor(colors.warningColor);
    colors_[ColorSlot(RenderColorId::MutedText)] = ToRenderColor(colors.mutedTextColor);
    colors_[ColorSlot(RenderColorId::Track)] = ToRenderColor(colors.trackColor);
    colors_[ColorSlot(RenderColorId::LayoutGuide)] = ToRenderColor(colors.layoutGuideColor);
    colors_[ColorSlot(RenderColorId::ActiveEdit)] = ToRenderColor(colors.activeEditColor);
    colors_[ColorSlot(RenderColorId::PanelBorder)] = ToRenderColor(colors.panelBorderColor);
    colors_[ColorSlot(RenderColorId::PanelFill)] = ToRenderColor(colors.panelFillColor);
    colors_[ColorSlot(RenderColorId::GraphBackground)] = ToRenderColor(colors.graphBackgroundColor);
    colors_[ColorSlot(RenderColorId::GraphMarker)] = ToRenderColor(colors.graphMarkerColor);
    colors_[ColorSlot(RenderColorId::GraphAxis)] = ToRenderColor(colors.graphAxisColor);
    colors_[ColorSlot(RenderColorId::LayoutGuideCalloutLeader)] = ToRenderColor(layoutGuideSheet.calloutLeaderColor);
    colors_[ColorSlot(RenderColorId::LayoutGuideCalloutFill)] = ToRenderColor(layoutGuideSheet.calloutFillColor);
    colors_[ColorSlot(RenderColorId::LayoutGuideCalloutBorder)] = ToRenderColor(layoutGuideSheet.calloutBorderColor);
    colors_[ColorSlot(RenderColorId::LayoutGuideCalloutParameter)] =
        ToRenderColor(layoutGuideSheet.calloutParameterColor);
    colors_[ColorSlot(RenderColorId::LayoutGuideCalloutDescription)] =
        ToRenderColor(layoutGuideSheet.calloutDescriptionColor);
}

const RenderColor& RendererPalette::Get(RenderColorId id) const {
    return colors_[ColorSlot(id)];
}
