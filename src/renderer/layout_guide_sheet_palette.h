#pragma once

#include "config/config.h"

class D2DRenderer;
class Renderer;
class RendererPalette;

void RebuildLayoutGuideSheetColors(RendererPalette& palette, const LayoutGuideSheetConfig& layoutGuideSheet);
void RebuildLayoutGuideSheetRendererPalette(D2DRenderer& renderer, const LayoutGuideSheetConfig& layoutGuideSheet);
void ApplyLayoutGuideSheetRendererPalette(Renderer& renderer, const LayoutGuideSheetConfig& layoutGuideSheet);
