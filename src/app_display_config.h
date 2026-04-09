#pragma once

#include <filesystem>
#include <ostream>

#include "config.h"

bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream);
int RunElevatedConfigureDisplayMode(const std::filesystem::path& sourceConfigPath, const std::filesystem::path& sourceDumpPath,
    const std::filesystem::path& targetConfigPath, const std::filesystem::path& targetImagePath);
