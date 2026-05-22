#pragma once

#include <windows.h>

COLORREF BlendNativeThemeColor(COLORREF foreground, COLORREF background, int foregroundPercent);
COLORREF ResolveNativeThemeSelectedBackground(COLORREF background, COLORREF highlight);
