#pragma once

struct TelemetryCollectorState;

void ResolveNetworkSelection(TelemetryCollectorState& state);
void CollectNetworkMetrics(TelemetryCollectorState& state, bool initializeOnly);
