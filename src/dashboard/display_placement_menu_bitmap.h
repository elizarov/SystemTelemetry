#pragma once

#include <windows.h>

#include "display/monitor.h"

struct DisplayPlacementMenuBitmapPixel {
    BYTE blue = 0;
    BYTE green = 0;
    BYTE red = 0;
    BYTE alpha = 255;

    bool operator==(const DisplayPlacementMenuBitmapPixel& other) const = default;
};

DisplayPlacementMenuBitmapPixel OpaqueDisplayPlacementMenuBitmapPixel(COLORREF color);
int ResolveNativeMenuBitmapSize(UINT dpi);
void PaintDisplayPlacementMenuBitmapPixels(DisplayPlacementMenuBitmapPixel* pixels,
    int bitmapSize,
    const DisplayMenuOption& option,
    COLORREF menuColor,
    COLORREF menuTextColor,
    COLORREF highlightColor);
HBITMAP CreateDisplayPlacementMenuBitmap(const DisplayMenuOption& option, UINT dpi);
