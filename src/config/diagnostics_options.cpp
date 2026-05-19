#include "config/diagnostics_options.h"

#include "config/config_def.h"

bool DiagnosticsOptions::HasAnyOutput() const {
    return trace || dump || screenshot || layoutGuideSheet || appIcon || saveConfig || saveFullConfig;
}

void ApplyDiagnosticsScaleOverride(AppConfig& config, const DiagnosticsOptions& options) {
    if (options.hasScaleOverride) {
        config.display.scale = options.scale;
    }
}
