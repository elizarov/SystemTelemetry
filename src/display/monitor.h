#pragma once

#include <windows.h>

#include <optional>
#include <string>

#include "config/config_primitives.h"
#include "util/scale.h"

struct AppConfig;
struct DisplayConfig;

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

enum class DisplayPlacementMode {
    FullScreen,
    Top,
    Bottom,
    Left,
    Right,
};

struct DisplayMenuMonitorInfo {
    std::string displayName;
    std::string configMonitorName;
    RECT rect{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
};

struct DisplayMenuOption {
    std::string label;
    std::string configMonitorName;
    RECT monitorRect{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool startsSection = false;
    DisplayPlacementMode placementMode = DisplayPlacementMode::FullScreen;
    SIZE targetSize{};
    LogicalPointConfig position{};
    double targetScale = 0.0;
    bool writesWallpaper = false;
    bool matchesCurrentConfig = false;
};

struct DisplayPlacementSchematicGeometry {
    RECT displayRect{};
    RECT caseDashRect{};
    RECT dividerRect{};
    bool hasDivider = false;
};

bool RectsEqual(const RECT& lhs, const RECT& rhs);
UINT GetMonitorDpi(HMONITOR monitor);
double ResolveDisplayScale(const AppConfig& config, UINT dpi);
SIZE ComputeWindowSizeForScale(const AppConfig& config, double scale);
SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi);
double ComputeMonitorFittedScale(const AppConfig& config, LONG monitorWidth, LONG monitorHeight);
double ComputeAspectResizeScale(SIZE layoutLogicalSize, POINT physicalExtent);
DisplayConfig BuildResizePlacementDisplayConfig(
    const DisplayConfig& display, const MonitorPlacementInfo& placement, double targetScale);
DisplayPlacementSchematicGeometry ComputeDisplayPlacementSchematicGeometry(
    const DisplayMenuOption& option, const RECT& bounds);
std::string SimplifyDeviceName(const std::string& deviceName);
bool IsUsefulFriendlyName(const std::string& name);
size_t BuildDisplayMenuOptionsForMonitor(const AppConfig& config,
    const DisplayMenuMonitorInfo& monitor,
    const std::optional<TargetMonitorInfo>& configuredMonitor,
    bool startsSection,
    DisplayMenuOption* options,
    size_t capacity);
AppConfig BuildConfiguredDisplayConfig(const AppConfig& config, const DisplayMenuOption& option);
bool ShouldClearPreviousDisplayWallpaper(const AppConfig& previousConfig,
    const std::optional<TargetMonitorInfo>& previousMonitor,
    const DisplayMenuOption& option);
size_t EnumerateDisplayMenuOptions(const AppConfig& config, DisplayMenuOption* options, size_t capacity);
std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName);
MonitorPlacementInfo GetMonitorPlacementForRect(const RECT& screenRect, double configuredScale = 0.0);
MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd, double configuredScale = 0.0);
