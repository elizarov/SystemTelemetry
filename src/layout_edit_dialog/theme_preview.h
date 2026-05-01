#pragma once

#include <windows.h>

#include "config/config.h"

const ThemeConfig* FindActiveThemeConfig(const AppConfig& config);
void DrawThemePreviewTriangle(HDC dc, const RECT& rect, const ThemeConfig& theme);
