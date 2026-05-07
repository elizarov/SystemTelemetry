#pragma once

#include <windows.h>

#include <optional>
#include <string>

#include "config/config.h"
#include "util/scale.h"

struct MonitorPlacementInfo {
    std::string deviceName;
    std::string monitorName = "Unknown";
    std::string configMonitorName;
    RECT monitorRect{};
    POINT relativePosition{};
    POINT physicalRelativePosition{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
};

struct TargetMonitorInfo {
    RECT rect{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
};

struct DisplayMenuOption {
    std::string displayName;
    std::string configMonitorName;
    RECT rect{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool layoutFits = false;
    bool matchesCurrentConfig = false;
    double fittedScale = 0.0;
};

bool RectsEqual(const RECT& lhs, const RECT& rhs);
UINT GetMonitorDpi(HMONITOR monitor);
double ResolveDisplayScale(const AppConfig& config, UINT dpi);
SIZE ComputeWindowSizeForScale(const AppConfig& config, double scale);
SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi);
double ComputeMonitorFittedScale(const AppConfig& config, LONG monitorWidth, LONG monitorHeight);
std::string SimplifyDeviceName(const std::string& deviceName);
bool IsUsefulFriendlyName(const std::string& name);
size_t EnumerateDisplayMenuOptions(const AppConfig& config, DisplayMenuOption* options, size_t capacity);
std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName);
MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd, double configuredScale = 0.0);
