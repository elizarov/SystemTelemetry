#include "display/monitor.h"

#include <cmath>
#include <vector>

#include "config/config.h"
#include "util/scale.h"
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
    const bool hasConfiguredWallpaper = !config.display.wallpaper.empty();
    const bool isConfiguredAtOrigin = config.display.position.x == 0 && config.display.position.y == 0;

    struct SearchContext {
        const AppConfig* config = nullptr;
        const std::optional<TargetMonitorInfo>* configuredMonitor = nullptr;
        DisplayMenuOption* options = nullptr;
        size_t capacity = 0;
        size_t count = 0;
        bool hasConfiguredWallpaper = false;
        bool isConfiguredAtOrigin = false;
    } context{&config, &configuredMonitor, options, capacity, 0, hasConfiguredWallpaper, isConfiguredAtOrigin};

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
            const LONG monitorWidth = info.rcMonitor.right - info.rcMonitor.left;
            const LONG monitorHeight = info.rcMonitor.bottom - info.rcMonitor.top;
            const UINT dpi = GetMonitorDpi(monitor);
            const double fittedScale = ComputeMonitorFittedScale(*context->config, monitorWidth, monitorHeight);

            DisplayMenuOption& option = context->options[context->count++];
            option.displayName = FormatText("%s (%ldx%ld)", identity.displayName.c_str(), monitorWidth, monitorHeight);
            option.configMonitorName = !identity.configName.empty() ? identity.configName : deviceName;
            option.rect = info.rcMonitor;
            option.dpi = dpi;
            option.layoutFits = fittedScale > 0.0;
            option.matchesCurrentConfig = context->hasConfiguredWallpaper && context->isConfiguredAtOrigin &&
                                          context->configuredMonitor->has_value() &&
                                          RectsEqual((*context->configuredMonitor)->rect, option.rect);
            option.fittedScale = fittedScale;
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

MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd, double configuredScale) {
    MonitorPlacementInfo info;
    RECT windowRect{};
    GetWindowRect(hwnd, &windowRect);

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXA monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoA(monitor, &monitorInfo)) {
        info.deviceName = monitorInfo.szDevice;
        const MonitorIdentity identity = GetMonitorIdentity(info.deviceName);
        info.monitorName = identity.displayName;
        info.configMonitorName = identity.configName;
        info.monitorRect = monitorInfo.rcMonitor;
        info.dpi = GetMonitorDpi(monitor);
        info.physicalRelativePosition.x = windowRect.left - monitorInfo.rcMonitor.left;
        info.physicalRelativePosition.y = windowRect.top - monitorInfo.rcMonitor.top;
        info.relativePosition.x = ScalePhysicalToLogical(
            windowRect.left - monitorInfo.rcMonitor.left, ResolveDisplayScale(configuredScale, info.dpi));
        info.relativePosition.y = ScalePhysicalToLogical(
            windowRect.top - monitorInfo.rcMonitor.top, ResolveDisplayScale(configuredScale, info.dpi));
    }
    return info;
}
