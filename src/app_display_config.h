#pragma once

#include <filesystem>
#include <ostream>

#include "config/config.h"
#include "snapshot_dump.h"

bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream);
bool ConfigureDisplay(
    const AppConfig& config, const TelemetryDump& dump, double targetScale, std::ostream* traceStream, HWND owner);
int RunElevatedConfigureDisplayMode(const std::filesystem::path& sourceConfigPath,
    const std::filesystem::path& sourceDumpPath,
    const std::filesystem::path& targetConfigPath,
    const std::filesystem::path& targetImagePath);
