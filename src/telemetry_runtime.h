#pragma once

#include <filesystem>
#include <memory>
#include <ostream>

#include "config.h"
#include "telemetry.h"

enum class DiagnosticsLayoutSimilarityMode {
    None,
    HorizontalSizes,
    VerticalSizes,
};

struct DiagnosticsOptions {
    bool trace = false;
    bool dump = false;
    bool screenshot = false;
    bool exit = false;
    bool fake = false;
    bool blank = false;
    bool editLayout = false;
    bool reload = false;
    bool defaultConfig = false;
    bool saveConfig = false;
    bool saveFullConfig = false;
    bool hasScaleOverride = false;
    DiagnosticsLayoutSimilarityMode layoutSimilarityMode = DiagnosticsLayoutSimilarityMode::None;
    double scale = 1.0;
    std::string layoutName;
    std::string editLayoutWidgetName;
    std::filesystem::path tracePath;
    std::filesystem::path dumpPath;
    std::filesystem::path screenshotPath;
    std::filesystem::path saveConfigPath;
    std::filesystem::path saveFullConfigPath;
    std::filesystem::path fakePath;

    bool HasAnyOutput() const {
        return trace || dump || screenshot || saveConfig || saveFullConfig;
    }
};

class TelemetryRuntime {
public:
    virtual ~TelemetryRuntime() = default;
    virtual bool Initialize(const AppConfig& config, std::ostream* traceStream) = 0;
    virtual const SystemSnapshot& Snapshot() const = 0;
    virtual TelemetryDump Dump() const = 0;
    virtual AppConfig EffectiveConfig() const = 0;
    virtual const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const = 0;
    virtual const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const = 0;
    virtual void SetEffectiveConfig(const AppConfig& config) = 0;
    virtual void SetPreferredNetworkAdapterName(const std::string& adapterName) = 0;
    virtual void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) = 0;
    virtual void RefreshSelections() = 0;
    virtual void UpdateSnapshot() = 0;
};

std::unique_ptr<TelemetryRuntime> CreateTelemetryRuntime(
    const DiagnosticsOptions& options, const std::filesystem::path& workingDirectory);

bool ShouldShowRuntimeDialogs(const DiagnosticsOptions& options);
