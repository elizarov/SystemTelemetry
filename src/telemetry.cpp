#define NOMINMAX
#include "telemetry.h"

#include "snapshot_dump.h"
#include "telemetry/collector_fake.h"
#include "telemetry/collector_real.h"

namespace {

bool ShouldShowCollectorDialogs(const DiagnosticsOptions& options) {
    return !options.trace;
}

}  // namespace

void TelemetryCollector::WriteDump(std::ostream& output) const {
    WriteTelemetryDump(output, Dump());
}

std::unique_ptr<TelemetryCollector> CreateTelemetryCollector(
    const DiagnosticsOptions& options, const std::filesystem::path& workingDirectory) {
    if (options.fake) {
        return CreateFakeTelemetryCollector(workingDirectory, options.fakePath, ShouldShowCollectorDialogs(options));
    }
    return CreateRealTelemetryCollector();
}
