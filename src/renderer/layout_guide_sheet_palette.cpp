#include "renderer/layout_guide_sheet_palette.h"

#include <cstddef>
#include <cstdint>

#include "renderer/impl/d2d_renderer.h"
#include "renderer/impl/palette.h"

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

void RebuildLayoutGuideSheetColors(RendererPalette& palette, const LayoutGuideSheetConfig& layoutGuideSheet) {
    palette.colors_[ColorSlot(RenderColorId::LayoutGuideCalloutLeader)] =
        ToRenderColor(layoutGuideSheet.calloutLeaderColor);
    palette.colors_[ColorSlot(RenderColorId::LayoutGuideCalloutFill)] =
        ToRenderColor(layoutGuideSheet.calloutFillColor);
    palette.colors_[ColorSlot(RenderColorId::LayoutGuideCalloutBorder)] =
        ToRenderColor(layoutGuideSheet.calloutBorderColor);
    palette.colors_[ColorSlot(RenderColorId::LayoutGuideCalloutParameter)] =
        ToRenderColor(layoutGuideSheet.calloutParameterColor);
    palette.colors_[ColorSlot(RenderColorId::LayoutGuideCalloutDescription)] =
        ToRenderColor(layoutGuideSheet.calloutDescriptionColor);
}

void RebuildLayoutGuideSheetRendererPalette(D2DRenderer& renderer, const LayoutGuideSheetConfig& layoutGuideSheet) {
    RebuildLayoutGuideSheetColors(renderer.palette_, layoutGuideSheet);
    renderer.d2dCache_.Clear();
}

void ApplyLayoutGuideSheetRendererPalette(Renderer& renderer, const LayoutGuideSheetConfig& layoutGuideSheet) {
    RebuildLayoutGuideSheetRendererPalette(static_cast<D2DRenderer&>(renderer), layoutGuideSheet);
}
