#pragma once

#include <windows.h>

inline constexpr UINT kDefaultDpi = USER_DEFAULT_SCREEN_DPI;

double ScaleFromDpi(UINT dpi);
bool HasExplicitDisplayScale(double scale);
double ResolveDisplayScale(double configuredScale, UINT dpi);
int ScaleLogicalToPhysical(int logicalValue, double scale);
int ScaleLogicalToPhysical(int logicalValue, UINT dpi);
int ScalePhysicalToLogical(int physicalValue, double scale);
int ScalePhysicalToLogical(int physicalValue, UINT dpi);
