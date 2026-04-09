#include "app_monitor.h"

#include <algorithm>
#include <cmath>

#include "app_constants.h"
#include "app_strings.h"
#include "utf8.h"

bool RectsEqual(const RECT& lhs, const RECT& rhs) {
    return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
}

using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);

UINT GetMonitorDpi(HMONITOR monitor) {
    if (monitor == nullptr) {
        return kDefaultDpi;
    }

    static GetDpiForMonitorFn getDpiForMonitor = []() -> GetDpiForMonitorFn {
        HMODULE module = LoadLibraryW(L"Shcore.dll");
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

double ScaleFromDpi(UINT dpi) {
    return static_cast<double>(std::max(kDefaultDpi, dpi)) / static_cast<double>(kDefaultDpi);
}

int ScaleLogicalToPhysical(int logicalValue, UINT dpi) {
    if (logicalValue == 0) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<double>(logicalValue) * ScaleFromDpi(dpi)));
}

int ScalePhysicalToLogical(int physicalValue, UINT dpi) {
    if (physicalValue == 0) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<double>(physicalValue) / ScaleFromDpi(dpi)));
}

SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi) {
    return SIZE{
        ScaleLogicalToPhysical(config.layout.structure.window.width, dpi),
        ScaleLogicalToPhysical(config.layout.structure.window.height, dpi)
    };
}

std::string SimplifyDeviceName(const std::string& deviceName) {
    if (deviceName.rfind("\\\\.\\", 0) == 0) {
        return deviceName.substr(4);
    }
    return deviceName;
}

bool IsUsefulFriendlyName(const std::string& name) {
    const std::string lowered = ToLower(name);
    return !name.empty() &&
        lowered != "generic pnp monitor" &&
        lowered.find("\\\\?\\display") != 0;
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

        const std::wstring wideDeviceName = WideFromUtf8(deviceName);
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

        const std::string friendlyName = Utf8FromWide(targetName.monitorFriendlyDeviceName);
        const std::string monitorPath = Utf8FromWide(targetName.monitorDevicePath);
        if (IsUsefulFriendlyName(friendlyName)) {
            identity.displayName = friendlyName + " (" + SimplifyDeviceName(deviceName) + ")";
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

std::vector<DisplayMenuOption> EnumerateDisplayMenuOptions(const AppConfig& config) {
    struct SearchContext {
        const AppConfig* config = nullptr;
        std::vector<DisplayMenuOption> results;
    } context{&config, {}};

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* context = reinterpret_cast<SearchContext*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) {
                return TRUE;
            }

            const std::string deviceName = Utf8FromWide(info.szDevice);
            const MonitorIdentity identity = GetMonitorIdentity(deviceName);
            const UINT dpi = GetMonitorDpi(monitor);
            const SIZE scaledWindow = ComputeWindowSizeForDpi(*context->config, dpi);
            const LONG monitorWidth = info.rcMonitor.right - info.rcMonitor.left;
            const LONG monitorHeight = info.rcMonitor.bottom - info.rcMonitor.top;

            DisplayMenuOption option;
            option.commandId = kCommandConfigureDisplayBase + static_cast<UINT>(context->results.size());
            option.displayName = identity.displayName + " (" + std::to_string(monitorWidth) + "x" + std::to_string(monitorHeight) + ")";
            option.configMonitorName = !identity.configName.empty() ? identity.configName : deviceName;
            option.rect = info.rcMonitor;
            option.dpi = dpi;
            option.layoutFits = scaledWindow.cx == monitorWidth && scaledWindow.cy == monitorHeight;
            context->results.push_back(std::move(option));
            return context->results.size() < (kCommandConfigureDisplayMax - kCommandConfigureDisplayBase + 1);
        },
        reinterpret_cast<LPARAM>(&context));

    return context.results;
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
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* context = reinterpret_cast<SearchContext*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) {
                return TRUE;
            }

            const std::string deviceName = Utf8FromWide(info.szDevice);
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

MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd) {
    MonitorPlacementInfo info;
    RECT windowRect{};
    GetWindowRect(hwnd, &windowRect);

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        info.deviceName = Utf8FromWide(monitorInfo.szDevice);
        const MonitorIdentity identity = GetMonitorIdentity(info.deviceName);
        info.monitorName = identity.displayName;
        info.configMonitorName = identity.configName;
        info.monitorRect = monitorInfo.rcMonitor;
        info.dpi = GetMonitorDpi(monitor);
        info.relativePosition.x = ScalePhysicalToLogical(windowRect.left - monitorInfo.rcMonitor.left, info.dpi);
        info.relativePosition.y = ScalePhysicalToLogical(windowRect.top - monitorInfo.rcMonitor.top, info.dpi);
    }
    return info;
}
