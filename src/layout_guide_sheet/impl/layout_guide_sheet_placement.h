#pragma once

#include <optional>
#include <string>
#include <vector>

#include "layout_guide_sheet/impl/layout_guide_sheet_types.h"
#include "renderer/render_types.h"
#include "util/function_ref.h"
#include "widget/layout_edit_parameter_id.h"
#include "widget/layout_edit_types.h"

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

struct LayoutGuideSheetPlacementBlockTrace {
    size_t cardIndex = 0;
    int leaderScore = 0;
    int sideRepairPasses = 0;
    size_t leftCallouts = 0;
    size_t topCallouts = 0;
    size_t rightCallouts = 0;
    size_t bottomCallouts = 0;
};

enum class LayoutGuideSheetLeaderIntersectionKind {
    LeaderCross,
    TargetSafeZone,
};

struct LayoutGuideSheetLeaderIntersectionTrace {
    size_t sourceCardIndex = 0;
    LayoutGuideSheetLeaderIntersectionKind kind = LayoutGuideSheetLeaderIntersectionKind::LeaderCross;
    size_t firstCalloutIndex = 0;
    size_t secondCalloutIndex = 0;
    LayoutGuideSheetExitSide firstExitSide = LayoutGuideSheetExitSide::Right;
    LayoutGuideSheetExitSide secondExitSide = LayoutGuideSheetExitSide::Right;
};

struct LayoutGuideSheetPlacementResult {
    int sheetWidth = 0;
    int sheetHeight = 0;
    std::vector<LayoutGuideSheetPlacementBlockTrace> blocks;
    std::vector<LayoutGuideSheetLeaderIntersectionTrace> remainingIntersections;
};

using LayoutGuideSheetConstrainCalloutWidth =
    FunctionRef<void(LayoutGuideSheetPlacementCallout& callout, int constrainedWidth)>;

LayoutGuideSheetPlacementResult PlaceLayoutGuideSheetCallouts(
    std::vector<LayoutGuideSheetCardPlacement>& cardPlacements,
    std::vector<LayoutGuideSheetPlacementCallout>& callouts,
    const LayoutGuideSheetPlacementStyle& style,
    const LayoutGuideSheetConstrainCalloutWidth& constrainCalloutWidth);
