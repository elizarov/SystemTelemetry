#pragma once

#include <windows.h>

#include <filesystem>

#include "config/config.h"
#include "diagnostics/snapshot_dump.h"
#include "util/trace.h"

bool ApplyConfiguredWallpaper(const AppConfig& config, Trace& trace);
bool ConfigureDisplay(const AppConfig& config, const TelemetryDump& dump, double targetScale, Trace& trace, HWND owner);
int RunElevatedConfigureDisplayMode(const std::filesystem::path& sourceConfigPath,
    const std::filesystem::path& sourceDumpPath,
    const std::filesystem::path& targetConfigPath,
    const std::filesystem::path& targetImagePath);
