#include "telemetry/impl/collector.h"

#include "telemetry/impl/collector_fake.h"
#include "telemetry/impl/collector_real.h"

std::unique_ptr<TelemetryCollector> CreateTelemetryCollector(
    const TelemetryCollectorOptions& options, const FilePath& workingDirectory, Trace& trace) {
    if (options.fake) {
        return CreateFakeTelemetryCollector(
            workingDirectory, options.fakePath, options.loadFakeDump, options.liveFake, trace);
    }
    return CreateRealTelemetryCollector(trace, options.synchronousProviderSamples);
}
