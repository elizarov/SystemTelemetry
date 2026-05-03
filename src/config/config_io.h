#pragma once

#include <windows.h>

#include "config/config.h"
#include "config/diagnostics_options.h"
#include "config/metric_catalog.h"
#include "util/file_path.h"

FilePath GetRuntimeConfigPath();
AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options, const ConfigParseContext& context);
