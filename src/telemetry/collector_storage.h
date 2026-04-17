#pragma once

#include <string>

struct TelemetryCollectorState;

std::string NormalizeStorageDriveLetter(const std::string& drive);
void ResolveStorageSelection(TelemetryCollectorState& state);
void UpdateStorageMetrics(TelemetryCollectorState& state, bool initializeOnly);
