#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

#include "config/config_primitives.h"

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

enum class DisplayResizeCorner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
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
    std::string autohide;
    RECT targetClientRect{};
    bool matchesCommittedConfig = false;
};

struct DisplayPlacementTarget {
    DisplayPlacementMode placementMode = DisplayPlacementMode::FullScreen;
    RECT monitorRect{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    SIZE targetSize{};
    LogicalPointConfig position{};
    double targetScale = 0.0;
    bool writesWallpaper = false;
    std::string autohide;
    RECT targetClientRect{};
};

struct DisplayPlacementSchematicGeometry {
    RECT displayRect{};
    RECT caseDashRect{};
    RECT dividerRect{};
    bool hasDivider = false;
};

struct DisplayWallpaperOwner {
    std::string monitorName;
    std::string wallpaper;
    RECT monitorRect{};
};

struct DisplayAspectResizeTarget {
    double targetScale = 1.0;
    RECT targetClientRect{};
};

bool RectsEqual(const RECT& lhs, const RECT& rhs);
UINT GetMonitorDpi(HMONITOR monitor);
double ResolveDisplayScale(const AppConfig& config, UINT dpi);
SIZE ComputeWindowSizeForScale(const AppConfig& config, double scale);
SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi);
double ComputeMonitorFittedScale(const AppConfig& config, LONG monitorWidth, LONG monitorHeight);
double ComputeAspectResizeScale(SIZE layoutLogicalSize, POINT physicalExtent);
DisplayAspectResizeTarget ComputeAspectResizeDragTarget(
    SIZE layoutLogicalSize, DisplayResizeCorner corner, POINT anchorScreenPoint, POINT draggedCornerScreenPoint);
DisplayConfig BuildResizePlacementDisplayConfig(
    const DisplayConfig& display, const MonitorPlacementInfo& placement, double targetScale);
const char* DisplayPlacementModeAutohideValue(DisplayPlacementMode mode);
std::optional<DisplayPlacementMode> DisplayPlacementModeFromAutohideValue(std::string_view value);
bool IsEdgeDisplayPlacementMode(DisplayPlacementMode mode);
std::optional<DisplayPlacementTarget> ComputeDisplayPlacementTarget(
    const AppConfig& config, const DisplayMenuMonitorInfo& monitor, DisplayPlacementMode mode);
std::optional<DisplayPlacementTarget> ResolveConfiguredAutohidePlacementTarget(const AppConfig& config);
bool DisplayPlacementTargetMatchesRect(const DisplayPlacementTarget& target, const RECT& rect);
DisplayPlacementSchematicGeometry ComputeDisplayPlacementSchematicGeometry(
    const DisplayMenuOption& option, const RECT& bounds);
std::string SimplifyDeviceName(const std::string& deviceName);
bool IsUsefulFriendlyName(const std::string& name);
size_t BuildDisplayMenuOptionsForMonitor(const AppConfig& config,
    const DisplayMenuMonitorInfo& monitor,
    const DisplayConfig* committedDisplay,
    const std::optional<TargetMonitorInfo>& committedMonitor,
    bool startsSection,
    DisplayMenuOption* options,
    size_t capacity);
size_t BuildDisplayMenuOptionsForMonitor(const AppConfig& config,
    const DisplayMenuMonitorInfo& monitor,
    const std::optional<TargetMonitorInfo>& committedMonitor,
    bool startsSection,
    DisplayMenuOption* options,
    size_t capacity);
AppConfig BuildConfiguredDisplayConfig(const AppConfig& config, const DisplayMenuOption& option);
bool ShouldClearPreviousDisplayWallpaper(const AppConfig& previousConfig,
    const std::optional<TargetMonitorInfo>& previousMonitor,
    const DisplayMenuOption& option);
std::optional<DisplayWallpaperOwner> ResolveCommittedDisplayWallpaperOwner(
    const AppConfig& config, const std::optional<TargetMonitorInfo>& monitor);
std::optional<DisplayWallpaperOwner> ResolveCommittedDisplayWallpaperOwner(const AppConfig& config);
AppConfig NormalizeCommittedDisplayWallpaperConfig(
    const AppConfig& config, const std::optional<TargetMonitorInfo>& monitor);
AppConfig NormalizeCommittedDisplayWallpaperConfig(const AppConfig& config);
bool ShouldClearCommittedDisplayWallpaper(
    const std::optional<DisplayWallpaperOwner>& previousOwner, const std::optional<DisplayWallpaperOwner>& nextOwner);
size_t EnumerateDisplayMenuOptions(
    const AppConfig& config, const DisplayConfig* committedDisplay, DisplayMenuOption* options, size_t capacity);
size_t EnumerateDisplayMenuOptions(const AppConfig& config, DisplayMenuOption* options, size_t capacity);
std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName);
MonitorPlacementInfo GetMonitorPlacementForRect(const RECT& screenRect, double configuredScale = 0.0);
MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd, double configuredScale = 0.0);
