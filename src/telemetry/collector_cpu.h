#pragma once

struct RealTelemetryCollectorState;

void InitializeCpuCollector(RealTelemetryCollectorState& state);
void UpdateCpuMetrics(RealTelemetryCollectorState& state);
