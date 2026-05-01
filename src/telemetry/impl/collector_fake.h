#pragma once

#include <memory>

#include "telemetry/telemetry.h"
#include "util/file_path.h"

std::unique_ptr<TelemetryCollector> CreateFakeTelemetryCollector(
    const FilePath& workingDirectory, const FilePath& configuredPath, TelemetryDumpLoader loadFakeDump, Trace& trace);
