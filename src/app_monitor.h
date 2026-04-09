#pragma once

#include <optional>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app_types.h"
#include "config.h"

bool RectsEqual(const RECT& lhs, const RECT& rhs);
UINT GetMonitorDpi(HMONITOR monitor);
double ScaleFromDpi(UINT dpi);
int ScaleLogicalToPhysical(int logicalValue, UINT dpi);
int ScalePhysicalToLogical(int physicalValue, UINT dpi);
SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi);
std::string SimplifyDeviceName(const std::string& deviceName);
bool IsUsefulFriendlyName(const std::string& name);
MonitorIdentity GetMonitorIdentity(const std::string& deviceName);
std::vector<DisplayMenuOption> EnumerateDisplayMenuOptions(const AppConfig& config);
std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName);
MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd);
