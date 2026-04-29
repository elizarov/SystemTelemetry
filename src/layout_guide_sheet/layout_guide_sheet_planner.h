#pragma once

#include <string>
#include <vector>

#include "config/config.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_guide_sheet/layout_guide_sheet_types.h"
#include "layout_model/layout_edit_active_region.h"

std::vector<std::string> SelectLayoutGuideSheetCards(const std::vector<LayoutGuideSheetCardSummary>& cards);

std::vector<LayoutGuideSheetCalloutRequest> BuildLayoutGuideSheetCallouts(const AppConfig& config,
    const LayoutEditActiveRegions& regions,
    const std::vector<LayoutGuideSheetCardSummary>& cards,
    const std::vector<std::string>& selectedCardIds);

std::vector<LayoutGuideSheetCalloutRequest> BuildLayoutGuideSheetOverviewCallouts(const AppConfig& config,
    const LayoutEditActiveRegions& regions,
    const std::vector<LayoutGuideSheetCardSummary>& cards);
