#pragma once

#include <string>

#include <windows.h>

struct MonitorPlacementInfo {
    std::string deviceName;
    std::string monitorName = "Unknown";
    std::string configMonitorName;
    RECT monitorRect{};
    POINT relativePosition{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
};

struct MonitorIdentity {
    std::string displayName;
    std::string configName;
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

struct LayoutMenuOption {
    UINT commandId = 0;
    std::string name;
};

struct NetworkMenuOption {
    UINT commandId = 0;
    std::string adapterName;
    std::string ipAddress;
    bool selected = false;
};

struct StorageDriveMenuOption {
    UINT commandId = 0;
    std::string driveLetter;
    std::string volumeLabel;
    double totalGb = 0.0;
    bool selected = false;
};
