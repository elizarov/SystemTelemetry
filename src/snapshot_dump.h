#pragma once

#include <istream>
#include <ostream>
#include <string>

#include "telemetry.h"

bool WriteTelemetryDump(std::ostream& output, const TelemetryDump& dump);
bool LoadTelemetryDump(std::istream& input, TelemetryDump& dump, std::string* error = nullptr);
