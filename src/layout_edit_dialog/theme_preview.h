#pragma once

#include <windows.h>

struct AppConfig;
struct ThemeConfig;

const ThemeConfig* FindActiveThemeConfig(const AppConfig& config);
void DrawThemePreviewTriangle(HDC dc, const RECT& rect, const ThemeConfig& theme);
