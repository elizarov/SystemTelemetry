#include "telemetry/impl/collector_state.h"

#include "telemetry/impl/hdi_gpu_performance.h"

RealTelemetryCollectorState::RealTelemetryCollectorState(
    Trace& trace, bool synchronousProviderSamples, const HardwareDependencyInjection* hardwareDependencyInjection)
    : trace_(trace), hardwareDependencyInjection_(hardwareDependencyInjection),
      synchronousProviderSamples_(synchronousProviderSamples) {}

RealTelemetryCollectorState::~RealTelemetryCollectorState() {
    if (cpu_.query != nullptr) {
        PdhCloseQuery(cpu_.query);
    }
    if (storage_.query != nullptr) {
        PdhCloseQuery(storage_.query);
    }
    WSACleanup();
}
