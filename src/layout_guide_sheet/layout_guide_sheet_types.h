#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "layout_model/layout_edit_active_region.h"
#include "renderer/render_types.h"

inline constexpr const char kLayoutGuideSheetOverviewSourceId[] = "__layout_overview";

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
    RenderRect targetRect{};
    int priority = 1000;
    size_t order = 0;
};
