#pragma once

#include "config/diagnostics_options.h"
#include "config/metric_catalog.h"
#include "util/file_path.h"

struct AppConfig;

FilePath GetRuntimeConfigPath();
AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options, const ConfigParseContext& context);
bool CanWriteRuntimeConfig(const FilePath& path);
