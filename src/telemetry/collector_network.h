#pragma once

struct RealTelemetryCollectorState;

void ResolveNetworkSelection(RealTelemetryCollectorState& state);
void UpdateNetworkMetrics(RealTelemetryCollectorState& state, bool initializeOnly);
