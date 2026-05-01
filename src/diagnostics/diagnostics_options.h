#pragma once

#include <optional>
#include <string>

#include "util/file_path.h"

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
    bool exit = false;
    bool fake = false;
    bool blank = false;
    bool editLayout = false;
    bool reload = false;
    bool defaultConfig = false;
    bool saveConfig = false;
    bool saveFullConfig = false;
    bool hasScaleOverride = false;
    std::optional<DiagnosticsHoverPoint> hoverPoint;
    DiagnosticsLayoutSimilarityMode layoutSimilarityMode = DiagnosticsLayoutSimilarityMode::None;
    double scale = 1.0;
    std::string layoutName;
    std::string themeName;
    std::string editLayoutWidgetName;
    FilePath tracePath;
    FilePath dumpPath;
    FilePath screenshotPath;
    FilePath layoutGuideSheetPath;
    FilePath saveConfigPath;
    FilePath saveFullConfigPath;
    FilePath fakePath;

    bool HasAnyOutput() const;
};
