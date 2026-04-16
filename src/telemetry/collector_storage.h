#pragma once

#include <string>

struct TelemetryCollectorState;

std::string NormalizeStorageDriveLetter(const std::string& drive);
void ResolveStorageSelection(TelemetryCollectorState& state);
void CollectStorageMetrics(TelemetryCollectorState& state, bool initializeOnly);
