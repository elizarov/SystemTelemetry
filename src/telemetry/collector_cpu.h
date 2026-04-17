#pragma once

struct TelemetryCollectorState;

void InitializeCpuCollector(TelemetryCollectorState& state);
void UpdateCpuMetrics(TelemetryCollectorState& state);
