#pragma once

#include <string_view>

#include "config/diagnostics_options.h"
#include "config/metric_catalog.h"
#include "util/file_path.h"

struct AppConfig;

FilePath GetRuntimeConfigPath();
AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options, const ConfigParseContext& context);
AppConfig LoadRuntimeConfigWithExtraTemplate(
    const DiagnosticsOptions& options, const ConfigParseContext& context, std::string_view extraTemplate);
bool CanWriteRuntimeConfig(const FilePath& path);
