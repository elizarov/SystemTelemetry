#include "display/monitor.h"

#include <cmath>
#include <vector>

#include "config/config.h"
#include "display/constants.h"
#include "util/strings.h"
#include "util/text_encoding.h"
#include "util/text_format.h"

namespace {

struct MonitorIdentity {
    std::string displayName;
    std::string configName;
};

constexpr double kMonitorFitEpsilon = 0.0001;
constexpr char kShcoreDllName[] = "Shcore.dll";

int RectWidth(const RECT& rect) {
    return rect.right - rect.left;
}

int RectHeight(const RECT& rect) {
    return rect.bottom - rect.top;
}

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) <= kMonitorFitEpsilon;
}

const char* DisplayPlacementModeLabel(DisplayPlacementMode mode) {
    switch (mode) {
        case DisplayPlacementMode::FullScreen:
            return "full screen";
        case DisplayPlacementMode::Top:
            return "top";
        case DisplayPlacementMode::Bottom:
            return "bottom";
        case DisplayPlacementMode::Left:
            return "left";
        case DisplayPlacementMode::Right:
            return "right";
    }
    return "";
}

}  // namespace

bool RectsEqual(const RECT& lhs, const RECT& rhs) {
    return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
}

using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);

UINT GetMonitorDpi(HMONITOR monitor) {
    if (monitor == nullptr) {
        return kDefaultDpi;
    }

    static GetDpiForMonitorFn getDpiForMonitor = []() -> GetDpiForMonitorFn {
        HMODULE module = LoadLibraryA(kShcoreDllName);
        if (module == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(module, "GetDpiForMonitor"));
    }();

    if (getDpiForMonitor != nullptr) {
        UINT dpiX = kDefaultDpi;
        UINT dpiY = kDefaultDpi;
        if (SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY))) {
            return dpiX;
        }
    }
    return kDefaultDpi;
}

double ResolveDisplayScale(const AppConfig& config, UINT dpi) {
    return ResolveDisplayScale(config.display.scale, dpi);
}

SIZE ComputeWindowSizeForScale(const AppConfig& config, double scale) {
    return SIZE{ScaleLogicalToPhysical(config.layout.structure.window.width, scale),
        ScaleLogicalToPhysical(config.layout.structure.window.height, scale)};
}

SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi) {
    return ComputeWindowSizeForScale(config, ResolveDisplayScale(config, dpi));
}

double ComputeMonitorFittedScale(const AppConfig& config, LONG monitorWidth, LONG monitorHeight) {
    if (config.layout.structure.window.width <= 0 || config.layout.structure.window.height <= 0 || monitorWidth <= 0 ||
        monitorHeight <= 0) {
        return 0.0;
    }

    const double widthScale =
        static_cast<double>(monitorWidth) / static_cast<double>(config.layout.structure.window.width);
    const double heightScale =
        static_cast<double>(monitorHeight) / static_cast<double>(config.layout.structure.window.height);
    if (!std::isfinite(widthScale) || !std::isfinite(heightScale) || widthScale <= 0.0 || heightScale <= 0.0) {
        return 0.0;
    }
    return std::abs(widthScale - heightScale) <= kMonitorFitEpsilon ? widthScale : 0.0;
}

size_t BuildDisplayMenuOptionsForMonitor(const AppConfig& config,
    const DisplayMenuMonitorInfo& monitor,
    const std::optional<TargetMonitorInfo>& configuredMonitor,
    bool startsSection,
    DisplayMenuOption* options,
    size_t capacity) {
    if (options == nullptr || capacity == 0 || config.layout.structure.window.width <= 0 ||
        config.layout.structure.window.height <= 0) {
        return 0;
    }

    const int monitorWidth = RectWidth(monitor.rect);
    const int monitorHeight = RectHeight(monitor.rect);
    if (monitorWidth <= 0 || monitorHeight <= 0) {
        return 0;
    }

    const double widthScale =
        static_cast<double>(monitorWidth) / static_cast<double>(config.layout.structure.window.width);
    const double heightScale =
        static_cast<double>(monitorHeight) / static_cast<double>(config.layout.structure.window.height);
    if (!std::isfinite(widthScale) || !std::isfinite(heightScale) || widthScale <= 0.0 || heightScale <= 0.0) {
        return 0;
    }

    const std::string labelName = !monitor.displayName.empty() ? monitor.displayName : monitor.configMonitorName;
    size_t count = 0;
    auto appendOption = [&](DisplayPlacementMode mode,
                            double targetScale,
                            SIZE targetSize,
                            LogicalPointConfig position,
                            bool writesWallpaper) {
        if (count >= capacity) {
            return;
        }

        DisplayMenuOption& option = options[count];
        option = {};
        option.label =
            FormatText("%s %dx%d %s", labelName.c_str(), targetSize.cx, targetSize.cy, DisplayPlacementModeLabel(mode));
        option.configMonitorName = monitor.configMonitorName;
        option.monitorRect = monitor.rect;
        option.dpi = monitor.dpi;
        option.startsSection = startsSection && count == 0;
        option.placementMode = mode;
        option.targetSize = targetSize;
        option.position = position;
        option.targetScale = targetScale;
        option.writesWallpaper = writesWallpaper;
        option.matchesCurrentConfig =
            configuredMonitor.has_value() && RectsEqual(configuredMonitor->rect, monitor.rect) &&
            AreScalesEqual(ResolveDisplayScale(config.display.scale, monitor.dpi), targetScale) &&
            config.display.position == position;
        ++count;
    };

    if (AreScalesEqual(widthScale, heightScale)) {
        appendOption(DisplayPlacementMode::FullScreen,
            widthScale,
            SIZE{monitorWidth, monitorHeight},
            LogicalPointConfig{},
            true);
        return count;
    }

    if (widthScale < heightScale) {
        const double targetScale = widthScale;
        const SIZE targetSize{monitorWidth, ScaleLogicalToPhysical(config.layout.structure.window.height, targetScale)};
        appendOption(DisplayPlacementMode::Top, targetScale, targetSize, LogicalPointConfig{}, false);
        appendOption(DisplayPlacementMode::Bottom,
            targetScale,
            targetSize,
            LogicalPointConfig{0, ScalePhysicalToLogical(monitorHeight - targetSize.cy, targetScale)},
            false);
        return count;
    }

    const double targetScale = heightScale;
    const SIZE targetSize{ScaleLogicalToPhysical(config.layout.structure.window.width, targetScale), monitorHeight};
    appendOption(DisplayPlacementMode::Left, targetScale, targetSize, LogicalPointConfig{}, false);
    appendOption(DisplayPlacementMode::Right,
        targetScale,
        targetSize,
        LogicalPointConfig{ScalePhysicalToLogical(monitorWidth - targetSize.cx, targetScale), 0},
        false);
    return count;
}

AppConfig BuildConfiguredDisplayConfig(const AppConfig& config, const DisplayMenuOption& option) {
    AppConfig updatedConfig = config;
    updatedConfig.display.monitorName = option.configMonitorName;
    updatedConfig.display.position = option.position;
    updatedConfig.display.scale = option.targetScale;
    updatedConfig.display.wallpaper = option.writesWallpaper ? kDefaultBlankWallpaperFileName : "";
    return updatedConfig;
}

bool ShouldClearPreviousDisplayWallpaper(const AppConfig& previousConfig,
    const std::optional<TargetMonitorInfo>& previousMonitor,
    const DisplayMenuOption& option) {
    if (previousConfig.display.wallpaper.empty()) {
        return false;
    }
    if (!option.writesWallpaper) {
        return true;
    }
    return !previousMonitor.has_value() || !RectsEqual(previousMonitor->rect, option.monitorRect);
}

std::string SimplifyDeviceName(const std::string& deviceName) {
    if (deviceName.rfind("\\\\.\\", 0) == 0) {
        return deviceName.substr(4);
    }
    return deviceName;
}

bool IsUsefulFriendlyName(const std::string& name) {
    const std::string lowered = ToLower(name);
    return !name.empty() && lowered != "generic pnp monitor" && lowered.find("\\\\?\\display") != 0;
}

MonitorIdentity GetMonitorIdentity(const std::string& deviceName) {
    MonitorIdentity identity;
    identity.displayName = SimplifyDeviceName(deviceName);
    identity.configName = deviceName;

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return identity;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS) {
        return identity;
    }

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = paths[i].sourceInfo.adapterId;
        sourceName.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS) {
            continue;
        }

        const std::wstring wideDeviceName = WideFromText(deviceName);
        if (_wcsicmp(sourceName.viewGdiDeviceName, wideDeviceName.c_str()) != 0) {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(targetName);
        targetName.header.adapterId = paths[i].targetInfo.adapterId;
        targetName.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&targetName.header) != ERROR_SUCCESS) {
            continue;
        }

        const std::string friendlyName = TextFromWide(targetName.monitorFriendlyDeviceName);
        const std::string monitorPath = TextFromWide(targetName.monitorDevicePath);
        if (IsUsefulFriendlyName(friendlyName)) {
            identity.displayName = FormatText("%s (%s)", friendlyName.c_str(), SimplifyDeviceName(deviceName).c_str());
            identity.configName = friendlyName;
        } else if (!monitorPath.empty()) {
            identity.displayName = SimplifyDeviceName(deviceName);
            identity.configName = monitorPath;
        } else {
            identity.displayName = SimplifyDeviceName(deviceName);
            identity.configName = deviceName;
        }
        return identity;
    }

    return identity;
}

size_t EnumerateDisplayMenuOptions(const AppConfig& config, DisplayMenuOption* options, size_t capacity) {
    const std::optional<TargetMonitorInfo> configuredMonitor = FindTargetMonitor(config.display.monitorName);

    struct SearchContext {
        const AppConfig* config = nullptr;
        const std::optional<TargetMonitorInfo>* configuredMonitor = nullptr;
        DisplayMenuOption* options = nullptr;
        size_t capacity = 0;
        size_t count = 0;
    } context{&config, &configuredMonitor, options, capacity, 0};

    EnumDisplayMonitors(
        nullptr,
        nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* context = reinterpret_cast<SearchContext*>(data);
            MONITORINFOEXA info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoA(monitor, &info)) {
                return TRUE;
            }
            if (context->count >= context->capacity) {
                return FALSE;
            }

            const std::string deviceName = info.szDevice;
            const MonitorIdentity identity = GetMonitorIdentity(deviceName);
            const DisplayMenuMonitorInfo monitorInfo{identity.displayName,
                !identity.configName.empty() ? identity.configName : deviceName,
                info.rcMonitor,
                GetMonitorDpi(monitor)};
            context->count += BuildDisplayMenuOptionsForMonitor(*context->config,
                monitorInfo,
                *context->configuredMonitor,
                context->count > 0,
                context->options + context->count,
                context->capacity - context->count);
            return context->count < context->capacity;
        },
        reinterpret_cast<LPARAM>(&context));

    return context.count;
}

std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName) {
    if (requestedName.empty()) {
        return std::nullopt;
    }

    struct SearchContext {
        std::string requestedName;
        std::optional<TargetMonitorInfo> result;
    } context{requestedName, std::nullopt};

    EnumDisplayMonitors(
        nullptr,
        nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* context = reinterpret_cast<SearchContext*>(data);
            MONITORINFOEXA info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoA(monitor, &info)) {
                return TRUE;
            }

            const std::string deviceName = info.szDevice;
            const MonitorIdentity identity = GetMonitorIdentity(deviceName);
            if (ContainsInsensitive(identity.displayName, context->requestedName) ||
                ContainsInsensitive(identity.configName, context->requestedName) ||
                ContainsInsensitive(deviceName, context->requestedName)) {
                context->result = TargetMonitorInfo{info.rcMonitor, GetMonitorDpi(monitor)};
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));

    return context.result;
}

MonitorPlacementInfo GetMonitorPlacementForRect(const RECT& screenRect, double configuredScale) {
    MonitorPlacementInfo info;

    HMONITOR monitor = MonitorFromRect(&screenRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXA monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoA(monitor, &monitorInfo)) {
        info.deviceName = monitorInfo.szDevice;
        const MonitorIdentity identity = GetMonitorIdentity(info.deviceName);
        info.monitorName = identity.displayName;
        info.configMonitorName = identity.configName;
        info.monitorRect = monitorInfo.rcMonitor;
        info.dpi = GetMonitorDpi(monitor);
        info.physicalRelativePosition.x = screenRect.left - monitorInfo.rcMonitor.left;
        info.physicalRelativePosition.y = screenRect.top - monitorInfo.rcMonitor.top;
        info.relativePosition.x = ScalePhysicalToLogical(
            screenRect.left - monitorInfo.rcMonitor.left, ResolveDisplayScale(configuredScale, info.dpi));
        info.relativePosition.y = ScalePhysicalToLogical(
            screenRect.top - monitorInfo.rcMonitor.top, ResolveDisplayScale(configuredScale, info.dpi));
    }
    return info;
}

MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd, double configuredScale) {
    RECT windowRect{};
    GetWindowRect(hwnd, &windowRect);
    return GetMonitorPlacementForRect(windowRect, configuredScale);
}
