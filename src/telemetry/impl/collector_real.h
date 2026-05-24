#pragma once

#include <memory>

#include "telemetry/impl/collector.h"

std::unique_ptr<TelemetryCollector> CreateRealTelemetryCollector(
    Trace& trace, bool synchronousProviderSamples, const HardwareDependencyInjection* hardwareDependencyInjection);
