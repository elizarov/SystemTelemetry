#pragma once

#include <windows.h>

inline constexpr UINT_PTR kRefreshTimerId = 1;
inline constexpr UINT_PTR kMoveTimerId = 2;
inline constexpr UINT_PTR kPlacementTimerId = 3;
inline constexpr UINT kRefreshTimerMs = 500;
inline constexpr UINT kMoveTimerMs = 16;
inline constexpr UINT kPlacementTimerMs = 2000;
inline constexpr UINT kTrayMessage = WM_APP + 1;
inline constexpr UINT kCommandMove = 1001;
inline constexpr UINT kCommandBringOnTop = 1002;
inline constexpr UINT kCommandReloadConfig = 1003;
inline constexpr UINT kCommandSaveConfig = 1004;
inline constexpr UINT kCommandExit = 1005;
inline constexpr UINT kCommandSaveDumpAs = 1006;
inline constexpr UINT kCommandSaveScreenshotAs = 1007;
inline constexpr UINT kCommandAutoStart = 1008;
inline constexpr UINT kCommandSaveFullConfigAs = 1009;
inline constexpr UINT kCommandEditLayout = 1010;
inline constexpr UINT kCommandLayoutBase = 1100;
inline constexpr UINT kCommandLayoutMax = 1199;
inline constexpr UINT kCommandNetworkAdapterBase = 1200;
inline constexpr UINT kCommandNetworkAdapterMax = 1299;
inline constexpr UINT kCommandStorageDriveBase = 1300;
inline constexpr UINT kCommandStorageDriveMax = 1399;
inline constexpr UINT kCommandScaleBase = 1400;
inline constexpr UINT kCommandScaleMax = 1499;
inline constexpr UINT kCommandCustomScale = 1500;
inline constexpr UINT kCommandConfigureDisplayBase = 2000;
inline constexpr UINT kCommandConfigureDisplayMax = 2099;
inline constexpr wchar_t kWindowClassName[] = L"SystemTelemetryDashboard";
inline constexpr wchar_t kDefaultTraceFileName[] = L"telemetry_trace.txt";
inline constexpr wchar_t kDefaultDumpFileName[] = L"telemetry_dump.txt";
inline constexpr wchar_t kDefaultScreenshotFileName[] = L"telemetry_screenshot.png";
inline constexpr wchar_t kDefaultSavedConfigFileName[] = L"telemetry_config.ini";
inline constexpr wchar_t kDefaultSavedFullConfigFileName[] = L"telemetry_full_config.ini";
inline constexpr wchar_t kDefaultBlankWallpaperFileName[] = L"telemetry_blank.png";
inline constexpr wchar_t kAutoStartRunSubKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kAutoStartValueName[] = L"SystemTelemetry";
inline constexpr UINT kDefaultDpi = USER_DEFAULT_SCREEN_DPI;
