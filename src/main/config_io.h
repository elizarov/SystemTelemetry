#pragma once

#include <filesystem>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "config/config.h"
#include "diagnostics/diagnostics_options.h"

std::filesystem::path GetRuntimeConfigPath();
AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options);
bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner);
