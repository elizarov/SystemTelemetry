#pragma once

struct TelemetryCollectorState;

void ResolveNetworkSelection(TelemetryCollectorState& state);
void UpdateNetworkMetrics(TelemetryCollectorState& state, bool initializeOnly);
