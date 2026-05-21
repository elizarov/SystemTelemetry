#pragma once

#include <windows.h>

#include "telemetry/telemetry.h"
#include "util/file_path.h"
#include "util/trace.h"

struct AppConfig;

bool ApplyConfiguredWallpaper(const AppConfig& config, Trace& trace);
bool ClearConfiguredWallpaper(const AppConfig& config, Trace& trace);
bool ConfigureDisplay(const AppConfig& config,
    const TelemetryDump& dump,
    double targetScale,
    bool writeWallpaper,
    const AppConfig* previousWallpaperConfig,
    Trace& trace,
    HWND owner);
int RunElevatedConfigureDisplayMode(
    const FilePath& configPayloadPath, const FilePath& dumpPayloadPath, bool writeWallpaper);
