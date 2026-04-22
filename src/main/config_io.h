#pragma once

#include <filesystem>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "config/config.h"
#include "config/metric_catalog.h"
#include "diagnostics/diagnostics_options.h"

std::filesystem::path GetRuntimeConfigPath();
ConfigParseContext RuntimeConfigParseContext();
AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options);
bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner);
