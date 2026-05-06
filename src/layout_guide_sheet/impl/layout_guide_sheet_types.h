#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "renderer/render_types.h"
#include "widget/layout_edit_parameter_id.h"
#include "widget/layout_edit_types.h"

inline constexpr const char kLayoutGuideSheetOverviewSourceId[] = "__layout_overview";

enum class LayoutGuideSheetExitSide {
    Left,
    Right,
    Top,
    Bottom,
};

struct LayoutGuideSheetCalloutRequest {
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
    int priority = 1000;
    size_t order = 0;
    std::optional<RenderRect> hoverArtifactTargetRect;
    std::optional<RenderRect> hoverAnchorRect;
    std::optional<LayoutEditGapAnchor> hoverGapAnchor;
    bool hoverAnchorDrawTargetOutline = true;
    bool targetAttachmentOnAnchorCircle = false;
    RenderRect bubbleRect{};
    RenderPoint targetAttachment{};
    RenderPoint bubbleAttachment{};
    LayoutGuideSheetExitSide exitSide = LayoutGuideSheetExitSide::Right;
    bool wrapDescription = false;
};
