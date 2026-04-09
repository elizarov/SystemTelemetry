#pragma once

#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "config.h"

COLORREF ToColorRef(unsigned int color);
SIZE MeasureTextSize(HDC hdc, HFONT font, const std::string& text);
int MeasureFontHeight(HDC hdc, HFONT font);
int MeasureWrappedTextHeight(HDC hdc, HFONT font, const std::string& text, int width);
void SetMenuItemRadioStyle(HMENU menu, UINT commandId);
HFONT CreateUiFont(const UiFontConfig& font);
