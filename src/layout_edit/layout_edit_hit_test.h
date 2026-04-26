#pragma once

#include <optional>
#include <vector>

#include "layout_model/layout_edit_active_region.h"
#include "widget/layout_edit_types.h"

struct LayoutEditHoverResolution {
    std::optional<LayoutEditWidgetIdentity> hoveredLayoutCard;
    std::optional<LayoutEditWidgetIdentity> hoveredEditableCard;
    std::optional<LayoutEditWidgetIdentity> hoveredEditableWidget;
    std::optional<LayoutEditGapAnchorKey> hoveredGapEditAnchor;
    std::optional<LayoutEditAnchorKey> hoveredEditableAnchor;
    std::optional<LayoutEditGapAnchor> hoveredGapEditAnchorRegion;
    std::optional<LayoutEditWidgetGuide> hoveredWidgetEditGuide;
    std::optional<LayoutEditGuide> hoveredLayoutGuide;
    std::optional<LayoutEditGapAnchorKey> actionableGapEditAnchor;
    std::optional<LayoutEditAnchorKey> actionableAnchorHandle;
};

std::optional<LayoutEditGuide> HitTestLayoutGuide(const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
std::optional<LayoutEditWidgetGuide> HitTestWidgetEditGuide(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
std::optional<LayoutEditGapAnchor> HitTestGapEditAnchor(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
std::optional<LayoutEditAnchorRegion> HitTestEditableAnchorTarget(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
std::optional<LayoutEditAnchorRegion> HitTestEditableAnchorHandle(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
std::optional<LayoutEditColorRegion> HitTestEditableColorRegion(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
std::optional<LayoutEditAnchorRegion> FindEditableAnchorRegion(
    const LayoutEditActiveRegions& regions, const LayoutEditAnchorKey& key);
std::optional<LayoutEditGapAnchor> FindGapEditAnchor(
    const LayoutEditActiveRegions& regions, const LayoutEditGapAnchorKey& key);
std::optional<LayoutEditGuide> FindLayoutEditGuide(
    const LayoutEditActiveRegions& regions, const LayoutEditGuide& guide);
std::optional<LayoutEditWidgetGuide> FindWidgetEditGuide(
    const LayoutEditActiveRegions& regions, const LayoutEditWidgetGuide& guide);
LayoutEditHoverResolution ResolveLayoutEditHover(const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
std::vector<LayoutGuideSnapCandidate> CollectLayoutGuideSnapCandidates(
    const LayoutEditActiveRegions& regions, const LayoutEditGuide& guide);
