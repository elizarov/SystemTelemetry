#pragma once

struct RealTelemetryCollectorState;

void InitializeGpuCollector(RealTelemetryCollectorState& state);
void ReconfigureGpuCollector(RealTelemetryCollectorState& state);
void UpdateGpuMetrics(RealTelemetryCollectorState& state);
