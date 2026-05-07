#pragma once

#include <vector>

#include "layout_model/layout_edit_active_region.h"
#include "widget/layout_edit_types.h"

const LayoutEditGuide* HitTestLayoutGuide(const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
const LayoutEditWidgetGuide* HitTestWidgetEditGuide(const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
const LayoutEditGapAnchor* HitTestGapEditAnchor(const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
const LayoutEditAnchorRegion* HitTestEditableAnchorTarget(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
const LayoutEditAnchorRegion* HitTestEditableAnchorHandle(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
const LayoutEditColorRegion* HitTestEditableColorRegion(
    const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
const LayoutEditAnchorRegion* FindEditableAnchorRegion(
    const LayoutEditActiveRegions& regions, const LayoutEditAnchorKey& key);
const LayoutEditGapAnchor* FindGapEditAnchor(const LayoutEditActiveRegions& regions, const LayoutEditGapAnchorKey& key);
const LayoutEditGuide* FindLayoutEditGuide(const LayoutEditActiveRegions& regions, const LayoutEditGuide& guide);
const LayoutEditWidgetGuide* FindWidgetEditGuide(
    const LayoutEditActiveRegions& regions, const LayoutEditWidgetGuide& guide);
LayoutEditHoverResolution ResolveLayoutEditHover(const LayoutEditActiveRegions& regions, RenderPoint clientPoint);
std::vector<LayoutGuideSnapCandidate> CollectLayoutGuideSnapCandidates(
    const LayoutEditActiveRegions& regions, const LayoutEditGuide& guide);
