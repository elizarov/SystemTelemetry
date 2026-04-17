#pragma once

struct TelemetryCollectorState;

void InitializeGpuCollector(TelemetryCollectorState& state);
void UpdateGpuMetrics(TelemetryCollectorState& state);
