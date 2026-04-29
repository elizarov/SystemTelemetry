#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "layout_guide_sheet/layout_guide_sheet_types.h"

class DashboardRenderer;
struct SystemSnapshot;

class LayoutGuideSheetRenderer {
public:
    explicit LayoutGuideSheetRenderer(DashboardRenderer& dashboardRenderer);

    bool SavePng(const std::filesystem::path& imagePath,
        const SystemSnapshot& snapshot,
        const std::vector<LayoutGuideSheetCalloutRequest>& calloutRequests,
        const std::vector<std::string>& selectedCardIds,
        std::vector<std::string>* traceDetails = nullptr,
        std::string* errorText = nullptr);

private:
    DashboardRenderer& dashboardRenderer_;
};
