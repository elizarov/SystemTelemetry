#include "config/diagnostics_options.h"

#include "config/config.h"
#include "util/scale.h"

bool DiagnosticsOptions::HasAnyOutput() const {
    return trace || dump || screenshot || layoutGuideSheet || appIcon || saveConfig || saveFullConfig;
}

void ApplyDiagnosticsScaleOverride(AppConfig& config, const DiagnosticsOptions& options) {
    if (options.hasScaleOverride) {
        config.display.scale = RoundDisplayScale(options.scale);
    }
}
