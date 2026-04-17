#pragma once

#include "telemetry_settings.h"

struct TelemetryCollectorState;

void InitializeBoardCollector(TelemetryCollectorState& state, const BoardTelemetrySettings& settings);
void ReconfigureBoardCollector(TelemetryCollectorState& state, const BoardTelemetrySettings& settings);
void UpdateBoardMetrics(TelemetryCollectorState& state);
