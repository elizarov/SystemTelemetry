#pragma once

#include <string>
#include <vector>

#include "config/config.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_types.h"
#include "layout_model/layout_edit_active_region.h"

std::vector<std::string> SelectLayoutGuideSheetCards(const std::vector<LayoutGuideSheetCardSummary>& cards);

void BuildLayoutGuideSheetCallouts(const AppConfig& config,
    const LayoutEditActiveRegions& regions,
    const std::vector<LayoutGuideSheetCardSummary>& cards,
    const std::vector<std::string>& selectedCardIds,
    std::vector<LayoutGuideSheetCalloutRequest>& callouts);

void BuildLayoutGuideSheetOverviewCallouts(const AppConfig& config,
    const LayoutEditActiveRegions& regions,
    std::vector<LayoutGuideSheetCalloutRequest>& callouts);

void AppendLayoutGuideSheetCardCallouts(std::vector<LayoutGuideSheetCalloutRequest>& merged,
    const std::vector<LayoutGuideSheetCalloutRequest>& cardCallouts);
