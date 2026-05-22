#pragma once

#include <memory>

#include "telemetry/impl/collector.h"
#include "util/file_path.h"

std::unique_ptr<TelemetryCollector> CreateFakeTelemetryCollector(
    const FilePath& workingDirectory,
    const FilePath& configuredPath,
    TelemetryDumpLoader loadFakeDump,
    bool liveSyntheticSource,
    Trace& trace);
