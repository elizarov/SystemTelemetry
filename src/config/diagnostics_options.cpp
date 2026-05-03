#include "config/diagnostics_options.h"

#include "config/config.h"

bool DiagnosticsOptions::HasAnyOutput() const {
    return trace || dump || screenshot || layoutGuideSheet || saveConfig || saveFullConfig;
}

void ApplyDiagnosticsScaleOverride(AppConfig& config, const DiagnosticsOptions& options) {
    if (options.hasScaleOverride) {
        config.display.scale = options.scale;
    }
}
