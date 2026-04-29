#pragma once

#include <optional>
#include <vector>

#include "layout_model/layout_edit_active_region.h"
#include "widget/layout_edit_types.h"

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
