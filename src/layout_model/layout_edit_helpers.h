#pragma once

#include <optional>

#include "widget/layout_edit_types.h"

bool MatchesWidgetIdentity(const LayoutEditWidgetIdentity& left, const LayoutEditWidgetIdentity& right);
bool MatchesParameterSubject(const LayoutEditParameterSubject& left, const LayoutEditParameterSubject& right);
bool MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right);
bool MatchesGapEditAnchorKey(const LayoutEditGapAnchorKey& left, const LayoutEditGapAnchorKey& right);
bool MatchesEditableAnchorKey(const LayoutEditAnchorKey& left, const LayoutEditAnchorKey& right);
bool MatchesWidgetEditGuide(const LayoutEditWidgetGuide& left, const LayoutEditWidgetGuide& right);
bool MatchesLayoutContainerEditKey(const LayoutContainerEditKey& left, const LayoutContainerEditKey& right);
bool MatchesLayoutWeightEditKey(const LayoutWeightEditKey& left, const LayoutWeightEditKey& right);
bool MatchesLayoutMetricEditKey(const LayoutMetricEditKey& left, const LayoutMetricEditKey& right);
bool MatchesLayoutCardTitleEditKey(const LayoutCardTitleEditKey& left, const LayoutCardTitleEditKey& right);
bool MatchesLayoutMetricListOrderEditKey(
    const LayoutMetricListOrderEditKey& left, const LayoutMetricListOrderEditKey& right);
bool MatchesCardChromeSelectionIdentity(
    const LayoutEditWidgetIdentity& selection, const LayoutEditWidgetIdentity& candidate);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& left, const LayoutEditFocusKey& right);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditGuide& guide);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditWidgetGuide& guide);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditGapAnchorKey& key);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditAnchorKey& key);
bool MatchesLayoutEditSelectionHighlight(const LayoutEditSelectionHighlight& highlight, const LayoutEditGuide& guide);
bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditWidgetGuide& guide);
bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditGapAnchorKey& key);
bool MatchesLayoutEditSelectionHighlight(const LayoutEditSelectionHighlight& highlight, const LayoutEditAnchorKey& key);
bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditColorRegion& region);
std::optional<LayoutEditParameter> LayoutEditAnchorParameter(const LayoutEditAnchorKey& key);
std::optional<LayoutMetricEditKey> LayoutEditAnchorMetricKey(const LayoutEditAnchorKey& key);
std::optional<LayoutCardTitleEditKey> LayoutEditAnchorCardTitleKey(const LayoutEditAnchorKey& key);
std::optional<LayoutMetricListOrderEditKey> LayoutEditAnchorMetricListOrderKey(const LayoutEditAnchorKey& key);
std::optional<LayoutContainerChildOrderEditKey> LayoutEditAnchorContainerChildOrderKey(const LayoutEditAnchorKey& key);
bool IsLayoutGuidePayload(const TooltipPayload& payload);
std::optional<LayoutEditParameter> TooltipPayloadParameter(const TooltipPayload& payload);
std::optional<double> TooltipPayloadNumericValue(const TooltipPayload& payload);
std::optional<unsigned int> TooltipPayloadColorValue(const TooltipPayload& payload);
RenderPoint TooltipPayloadAnchorPoint(const TooltipPayload& payload);
std::optional<LayoutEditFocusKey> TooltipPayloadFocusKey(const TooltipPayload& payload);
