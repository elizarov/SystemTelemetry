#pragma once

#include <cstdio>
#include <string>
#include <string_view>

#include "telemetry/telemetry.h"

bool WriteTelemetryDump(std::FILE* output, const TelemetryDump& dump);
bool WriteTelemetryDumpText(std::string& output, const TelemetryDump& dump);
bool LoadTelemetryDump(std::string_view input, TelemetryDump& dump, std::string* error = nullptr);
