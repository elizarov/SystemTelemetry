#pragma once

#include "config/telemetry_settings.h"

struct RealTelemetryCollectorState;

void InitializeBoardCollector(RealTelemetryCollectorState& state, const BoardTelemetrySettings& settings);
void ReconfigureBoardCollector(RealTelemetryCollectorState& state, const BoardTelemetrySettings& settings);
void UpdateBoardMetrics(RealTelemetryCollectorState& state);
