#pragma once

#include <filesystem>
#include <optional>
#include <string>

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
    std::string editLayoutWidgetName;
    std::filesystem::path tracePath;
    std::filesystem::path dumpPath;
    std::filesystem::path screenshotPath;
    std::filesystem::path saveConfigPath;
    std::filesystem::path saveFullConfigPath;
    std::filesystem::path fakePath;

    bool HasAnyOutput() const;
};
