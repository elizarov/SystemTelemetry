#pragma once

#include <windows.h>

#include "config/config.h"
#include "config/metric_catalog.h"
#include "diagnostics/diagnostics_options.h"
#include "util/file_path.h"

FilePath GetRuntimeConfigPath();
ConfigParseContext RuntimeConfigParseContext();
AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options);
bool SaveConfigElevated(const FilePath& targetPath, const AppConfig& config, HWND owner);
