#pragma once

#include <filesystem>
#include <memory>

#include "telemetry/telemetry.h"

std::unique_ptr<TelemetryCollector> CreateFakeTelemetryCollector(const std::filesystem::path& workingDirectory,
    const std::filesystem::path& configuredPath,
    TelemetryDumpLoader loadFakeDump);
