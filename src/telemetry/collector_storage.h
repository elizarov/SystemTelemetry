#pragma once

struct RealTelemetryCollectorState;

void InitializeStorageCollector(RealTelemetryCollectorState& state);
void ResolveStorageSelection(RealTelemetryCollectorState& state);
void UpdateStorageMetrics(RealTelemetryCollectorState& state, bool initializeOnly);
