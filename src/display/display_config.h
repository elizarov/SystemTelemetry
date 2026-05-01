#pragma once

#include <windows.h>

#include "config/config.h"
#include "diagnostics/snapshot_dump.h"
#include "util/file_path.h"
#include "util/trace.h"

bool ApplyConfiguredWallpaper(const AppConfig& config, Trace& trace);
bool ConfigureDisplay(const AppConfig& config, const TelemetryDump& dump, double targetScale, Trace& trace, HWND owner);
int RunElevatedConfigureDisplayMode(const FilePath& sourceConfigPath,
    const FilePath& sourceDumpPath,
    const FilePath& targetConfigPath,
    const FilePath& targetImagePath);
