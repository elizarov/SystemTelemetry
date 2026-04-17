#pragma once

#include <string>

struct TelemetryCollectorState;

void InitializeStorageCollector(TelemetryCollectorState& state);
std::string NormalizeStorageDriveLetter(const std::string& drive);
void ResolveStorageSelection(TelemetryCollectorState& state);
void UpdateStorageMetrics(TelemetryCollectorState& state, bool initializeOnly);
