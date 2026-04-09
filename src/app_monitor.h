#pragma once

#include <optional>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "config.h"

struct MonitorPlacementInfo {
    std::string deviceName;
    std::string monitorName = "Unknown";
    std::string configMonitorName;
    RECT monitorRect{};
    POINT relativePosition{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
};

struct TargetMonitorInfo {
    RECT rect{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
};

struct DisplayMenuOption {
    UINT commandId = 0;
    std::string displayName;
    std::string configMonitorName;
    RECT rect{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool layoutFits = false;
};

bool RectsEqual(const RECT& lhs, const RECT& rhs);
UINT GetMonitorDpi(HMONITOR monitor);
double ScaleFromDpi(UINT dpi);
int ScaleLogicalToPhysical(int logicalValue, UINT dpi);
int ScalePhysicalToLogical(int physicalValue, UINT dpi);
SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi);
std::string SimplifyDeviceName(const std::string& deviceName);
bool IsUsefulFriendlyName(const std::string& name);
std::vector<DisplayMenuOption> EnumerateDisplayMenuOptions(const AppConfig& config);
std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName);
MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd);
