#pragma once

#include <string>
#include <vector>

#include "layout_guide_sheet/impl/layout_guide_sheet_types.h"
#include "renderer/render_types.h"
#include "util/function_ref.h"

using LayoutGuideSheetPlacementCallout = LayoutGuideSheetCalloutRequest;

struct LayoutGuideSheetCardPlacement {
    std::string id;
    RenderRect sourceRect{};
    RenderRect destRect{};
    bool overview = false;
};

struct LayoutGuideSheetPlacementStyle {
    int sheetMargin = 0;
    int calloutGap = 0;
    int rowGap = 0;
    int blockGap = 0;
    int targetSafeRadius = 0;
    int gaugeRingThickness = 1;
};

struct LayoutGuideSheetPlacementResult {
    int sheetWidth = 0;
    int sheetHeight = 0;
};

using LayoutGuideSheetConstrainCalloutWidth =
    FunctionRef<void(LayoutGuideSheetPlacementCallout& callout, int constrainedWidth)>;

LayoutGuideSheetPlacementResult PlaceLayoutGuideSheetCallouts(
    std::vector<LayoutGuideSheetCardPlacement>& cardPlacements,
    std::vector<LayoutGuideSheetPlacementCallout>& callouts,
    const LayoutGuideSheetPlacementStyle& style,
    const LayoutGuideSheetConstrainCalloutWidth& constrainCalloutWidth,
    std::vector<std::string>* traceDetails);
