#include "config/config_io.h"

#include "config/config_parser.h"
#include "config/config_writer.h"
#include "util/paths.h"

FilePath GetRuntimeConfigPath() {
    return GetExecutableDirectory() / L"config.ini";
}

AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options, const ConfigParseContext& context) {
    AppConfig config = LoadConfig(GetRuntimeConfigPath(), !options.defaultConfig, context);
    ApplyDiagnosticsScaleOverride(config, options);
    return config;
}
