#include "telemetry/telemetry.h"

#include "telemetry/impl/collector_fake.h"
#include "telemetry/impl/collector_real.h"

std::unique_ptr<TelemetryCollector> CreateTelemetryCollector(
    const TelemetryCollectorOptions& options, const std::filesystem::path& workingDirectory) {
    if (options.fake) {
        return CreateFakeTelemetryCollector(workingDirectory, options.fakePath, options.loadFakeDump);
    }
    return CreateRealTelemetryCollector();
}
