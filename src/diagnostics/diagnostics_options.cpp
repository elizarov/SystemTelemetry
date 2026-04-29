#include "diagnostics/diagnostics_options.h"

bool DiagnosticsOptions::HasAnyOutput() const {
    return trace || dump || screenshot || layoutGuideSheet || saveConfig || saveFullConfig;
}
