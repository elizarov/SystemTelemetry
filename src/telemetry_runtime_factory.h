#pragma once

#include <filesystem>
#include <memory>

#include "diagnostics_options.h"
#include "telemetry_runtime.h"

std::unique_ptr<TelemetryRuntime> CreateTelemetryRuntime(
    const DiagnosticsOptions& options, const std::filesystem::path& workingDirectory);
