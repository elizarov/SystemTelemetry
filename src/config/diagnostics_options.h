#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "util/file_path.h"

struct AppConfig;
struct DiagnosticsOptions;

using DiagnosticsErrorHandlerFn = void (*)(const DiagnosticsOptions& options, std::string_view message);

enum class DiagnosticsLayoutSimilarityMode {
    None,
    HorizontalSizes,
    VerticalSizes,
};

struct DiagnosticsHoverPoint {
    int x = 0;
    int y = 0;
};

struct DiagnosticsOptions {
    bool trace = false;
    bool dump = false;
    bool screenshot = false;
    bool layoutGuideSheet = false;
    bool appIcon = false;
    bool exit = false;
    bool fake = false;
    bool blank = false;
    bool editLayout = false;
    bool defaultConfig = false;
    bool saveConfig = false;
    bool saveFullConfig = false;
    bool hasScaleOverride = false;
    bool hasAppIconSize = false;
    bool hasTracePrefixFilter = false;
    bool hasInvalidTracePrefixFilter = false;
    std::optional<DiagnosticsHoverPoint> hoverPoint;
    DiagnosticsLayoutSimilarityMode layoutSimilarityMode = DiagnosticsLayoutSimilarityMode::None;
    double scale = 1.0;
    int appIconSize = 256;
    std::uint64_t tracePrefixFilter = 0;
    std::string invalidTracePrefixFilterName;
    std::string layoutName;
    std::string themeName;
    std::string editLayoutWidgetName;
    FilePath tracePath;
    FilePath dumpPath;
    FilePath screenshotPath;
    FilePath layoutGuideSheetPath;
    FilePath appIconPath;
    FilePath saveConfigPath;
    FilePath saveFullConfigPath;
    FilePath fakePath;
    DiagnosticsErrorHandlerFn reportError = nullptr;

    bool HasAnyOutput() const;
};

void ApplyDiagnosticsScaleOverride(AppConfig& config, const DiagnosticsOptions& options);
