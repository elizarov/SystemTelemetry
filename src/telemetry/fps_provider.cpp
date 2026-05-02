#include "telemetry/fps_provider.h"

#include "telemetry/fps/fps_etw_provider.h"

std::unique_ptr<FpsTelemetryProvider> CreateFpsServiceTelemetryProvider(Trace& trace) {
    return CreatePresentedFpsEtwProvider(trace);
}
