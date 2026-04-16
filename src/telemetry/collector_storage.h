#pragma once

#include <string>

// Private storage collector module contract. The TelemetryCollector::Impl member
// declarations stay in collector_internal.h; this header owns the cross-file
// free helper that telemetry.cpp also uses.
std::string NormalizeStorageDriveLetter(const std::string& drive);
