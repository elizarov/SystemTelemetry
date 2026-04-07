#pragma once

#include <filesystem>
#include <memory>
#include <ostream>

#include "config.h"
#include "telemetry.h"

struct DiagnosticsOptions {
    bool trace = false;
    bool dump = false;
    bool screenshot = false;
    bool exit = false;
    bool fake = false;
    bool blank = false;
    bool reload = false;
    double scale = 1.0;
    std::string layoutName;
    std::filesystem::path tracePath;
    std::filesystem::path dumpPath;
    std::filesystem::path screenshotPath;
    std::filesystem::path fakePath;

    bool HasAnyOutput() const {
        return trace || dump || screenshot;
    }
};

class TelemetryRuntime {
public:
    virtual ~TelemetryRuntime() = default;
    virtual bool Initialize(const AppConfig& config, std::ostream* traceStream) = 0;
    virtual const SystemSnapshot& Snapshot() const = 0;
    virtual TelemetryDump Dump() const = 0;
    virtual AppConfig EffectiveConfig() const = 0;
    virtual void SetEffectiveConfig(const AppConfig& config) = 0;
    virtual void UpdateSnapshot() = 0;
};

std::unique_ptr<TelemetryRuntime> CreateTelemetryRuntime(
    const DiagnosticsOptions& options,
    const std::filesystem::path& workingDirectory);

bool ShouldShowRuntimeDialogs(const DiagnosticsOptions& options);
