#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "util/file_path.h"

class DashboardRenderer;
class Trace;
struct AppConfig;
struct SystemSnapshot;

bool LoadLayoutGuideSheetConfigText(std::string_view configText, LayoutGuideSheetConfig& guideSheet);
void ResolveLayoutGuideSheetColors(const AppConfig& config, LayoutGuideSheetConfig& guideSheet);

struct LayoutGuideSheetPipelineStats {
    std::chrono::nanoseconds activeRegions{};
    std::chrono::nanoseconds plan{};
    std::chrono::nanoseconds measure{};
    std::chrono::nanoseconds placement{};
    std::chrono::nanoseconds draw{};
    std::vector<std::string> traceDetails;
    size_t selectedCards = 0;
    size_t callouts = 0;
};

bool SaveLayoutGuideSheetPng(const FilePath& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    const LayoutGuideSheetConfig& guideSheet,
    double scale,
    Trace& trace,
    std::string* errorText,
    LayoutGuideSheetPipelineStats* stats = nullptr);

bool RenderLayoutGuideSheetOffscreen(DashboardRenderer& renderer,
    const SystemSnapshot& snapshot,
    const LayoutGuideSheetConfig& guideSheet,
    std::string* errorText = nullptr,
    LayoutGuideSheetPipelineStats* stats = nullptr);
