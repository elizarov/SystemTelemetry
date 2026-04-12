#include "telemetry_runtime_factory.h"

bool ShouldShowRuntimeDialogs(const DiagnosticsOptions& options) {
    return !options.trace;
}

std::unique_ptr<TelemetryRuntime> CreateRealTelemetryRuntime();
std::unique_ptr<TelemetryRuntime> CreateFakeTelemetryRuntime(
    const std::filesystem::path& workingDirectory, const std::filesystem::path& configuredPath, bool showDialogs);

std::unique_ptr<TelemetryRuntime> CreateTelemetryRuntime(
    const DiagnosticsOptions& options, const std::filesystem::path& workingDirectory) {
    if (options.fake) {
        return CreateFakeTelemetryRuntime(workingDirectory, options.fakePath, ShouldShowRuntimeDialogs(options));
    }
    return CreateRealTelemetryRuntime();
}
