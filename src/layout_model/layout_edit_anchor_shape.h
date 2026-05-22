#pragma once

#include "renderer/render_types.h"
#include "widget/layout_edit_types.h"

class Renderer;

void DrawLayoutEditAnchorShape(
    Renderer& renderer,
    AnchorShape shape,
    const RenderRect& rect,
    RenderColorId color,
    float outlineWidth,
    int gapHalf,
    bool outlineCircle,
    bool outlinePlus);
