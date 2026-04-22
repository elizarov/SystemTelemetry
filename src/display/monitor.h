#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "config/config.h"

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
    UINT commandId = 0;
    std::string displayName;
    std::string configMonitorName;
    RECT rect{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool layoutFits = false;
    double fittedScale = 0.0;
};

bool RectsEqual(const RECT& lhs, const RECT& rhs);
UINT GetMonitorDpi(HMONITOR monitor);
double ScaleFromDpi(UINT dpi);
bool HasExplicitDisplayScale(double scale);
double ResolveDisplayScale(double configuredScale, UINT dpi);
double ResolveDisplayScale(const AppConfig& config, UINT dpi);
int ScaleLogicalToPhysical(int logicalValue, double scale);
int ScaleLogicalToPhysical(int logicalValue, UINT dpi);
int ScalePhysicalToLogical(int physicalValue, double scale);
int ScalePhysicalToLogical(int physicalValue, UINT dpi);
SIZE ComputeWindowSizeForScale(const AppConfig& config, double scale);
SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi);
double ComputeMonitorFittedScale(const AppConfig& config, LONG monitorWidth, LONG monitorHeight);
std::string SimplifyDeviceName(const std::string& deviceName);
bool IsUsefulFriendlyName(const std::string& name);
std::vector<DisplayMenuOption> EnumerateDisplayMenuOptions(const AppConfig& config);
std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName);
MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd, double configuredScale = 0.0);
