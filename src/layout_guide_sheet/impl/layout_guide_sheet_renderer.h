#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "layout_guide_sheet/impl/layout_guide_sheet_types.h"
#include "layout_model/layout_edit_active_region.h"
#include "util/file_path.h"
#include "util/function_ref.h"

class DashboardRenderer;
struct LayoutGuideSheetConfig;
struct SystemSnapshot;

struct LayoutGuideSheetRenderStats {
    std::chrono::nanoseconds measure{};
    std::chrono::nanoseconds placement{};
    std::chrono::nanoseconds draw{};
};

class LayoutGuideSheetRenderer {
public:
    LayoutGuideSheetRenderer(DashboardRenderer& dashboardRenderer, const LayoutGuideSheetConfig& guideSheet);

    bool SavePng(const FilePath& imagePath,
        const SystemSnapshot& snapshot,
        std::vector<LayoutGuideSheetCalloutRequest>& callouts,
        const std::vector<std::string>& selectedCardIds,
        std::vector<std::string>* traceDetails = nullptr,
        std::string* errorText = nullptr,
        LayoutGuideSheetRenderStats* stats = nullptr);
    bool RenderOffscreen(const SystemSnapshot& snapshot,
        std::vector<LayoutGuideSheetCalloutRequest>& callouts,
        const std::vector<std::string>& selectedCardIds,
        std::vector<std::string>* traceDetails = nullptr,
        std::string* errorText = nullptr,
        LayoutGuideSheetRenderStats* stats = nullptr);
    LayoutEditActiveRegions CollectOverviewActiveRegions(const SystemSnapshot& snapshot);

private:
    using SurfaceDrawCallback = FunctionRef<void()>;
    using SurfaceRenderer = FunctionRef<bool(int width, int height, SurfaceDrawCallback draw)>;

    bool Render(const SystemSnapshot& snapshot,
        std::vector<LayoutGuideSheetCalloutRequest>& callouts,
        const std::vector<std::string>& selectedCardIds,
        const SurfaceRenderer& renderSurface,
        std::vector<std::string>* traceDetails,
        std::string* errorText,
        LayoutGuideSheetRenderStats* stats);

    DashboardRenderer& dashboardRenderer_;
    const LayoutGuideSheetConfig& guideSheet_;
};
