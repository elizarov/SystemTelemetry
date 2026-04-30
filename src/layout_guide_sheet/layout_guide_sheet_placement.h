#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "renderer/render_types.h"
#include "widget/layout_edit_parameter_id.h"
#include "widget/layout_edit_types.h"

enum class LayoutGuideSheetExitSide {
    Left,
    Right,
    Top,
    Bottom,
};

struct LayoutGuideSheetPlacementCallout {
    std::string key;
    std::string sourceCardId;
    std::string parameterLine;
    std::string descriptionLine;
    std::optional<LayoutEditAnchorKey> hoverAnchorKey;
    std::optional<LayoutEditWidgetGuide> hoverWidgetGuide;
    std::optional<LayoutEditGuide> hoverLayoutGuide;
    std::optional<LayoutEditGapAnchorKey> hoverGapAnchorKey;
    std::optional<AnchorShape> hoverAnchorShape;
    std::optional<LayoutEditParameter> hoverColorParameter;
    RenderRect targetRect{};
    std::optional<RenderRect> hoverArtifactTargetRect;
    std::optional<RenderRect> hoverAnchorRect;
    std::optional<LayoutEditGapAnchor> hoverGapAnchor;
    bool hoverAnchorDrawTargetOutline = true;
    bool targetAttachmentOnAnchorCircle = false;
    RenderRect bubbleRect{};
    RenderPoint targetAttachment{};
    RenderPoint bubbleAttachment{};
    LayoutGuideSheetExitSide exitSide = LayoutGuideSheetExitSide::Right;
    int priority = 1000;
    size_t order = 0;
    bool wrapDescription = false;
};

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
    std::string cardId;
    int leaderScore = 0;
    int sideRepairPasses = 0;
    size_t leftCallouts = 0;
    size_t topCallouts = 0;
    size_t rightCallouts = 0;
    size_t bottomCallouts = 0;
};

struct LayoutGuideSheetPlacementResult {
    int sheetWidth = 0;
    int sheetHeight = 0;
    std::vector<LayoutGuideSheetPlacementBlockTrace> blocks;
    std::vector<std::string> warningCalloutKeys;
};

using LayoutGuideSheetConstrainCalloutWidth =
    std::function<void(LayoutGuideSheetPlacementCallout& callout, int constrainedWidth)>;

LayoutGuideSheetPlacementResult PlaceLayoutGuideSheetCallouts(
    std::vector<LayoutGuideSheetCardPlacement>& cardPlacements,
    std::vector<LayoutGuideSheetPlacementCallout>& callouts,
    const LayoutGuideSheetPlacementStyle& style,
    const LayoutGuideSheetConstrainCalloutWidth& constrainCalloutWidth);
