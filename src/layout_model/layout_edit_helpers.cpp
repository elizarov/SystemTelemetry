#include "layout_model/layout_edit_helpers.h"

namespace {

bool MatchesLayoutEditAnchorSubject(const LayoutEditAnchorKey& left, const LayoutEditAnchorKey& right) {
    if (left.subject.index() != right.subject.index()) {
        return false;
    }
    if (const auto* leftParameter = std::get_if<LayoutEditParameter>(&left.subject)) {
        return *leftParameter == std::get<LayoutEditParameter>(right.subject);
    }
    if (const auto* leftMetric = std::get_if<LayoutMetricEditKey>(&left.subject)) {
        return MatchesLayoutMetricEditKey(*leftMetric, std::get<LayoutMetricEditKey>(right.subject));
    }
    if (const auto* leftCardTitle = std::get_if<LayoutCardTitleEditKey>(&left.subject)) {
        return MatchesLayoutCardTitleEditKey(*leftCardTitle, std::get<LayoutCardTitleEditKey>(right.subject));
    }
    if (const auto* leftMetricListOrder = std::get_if<LayoutMetricListOrderEditKey>(&left.subject)) {
        return MatchesLayoutMetricListOrderEditKey(
            *leftMetricListOrder, std::get<LayoutMetricListOrderEditKey>(right.subject));
    }
    return MatchesLayoutContainerEditKey(
        LayoutContainerEditKey{std::get<LayoutContainerChildOrderEditKey>(left.subject).editCardId,
            std::get<LayoutContainerChildOrderEditKey>(left.subject).nodePath},
        LayoutContainerEditKey{std::get<LayoutContainerChildOrderEditKey>(right.subject).editCardId,
            std::get<LayoutContainerChildOrderEditKey>(right.subject).nodePath});
}

}  // namespace

bool MatchesWidgetIdentity(const LayoutEditWidgetIdentity& left, const LayoutEditWidgetIdentity& right) {
    return left.kind == right.kind && left.renderCardId == right.renderCardId && left.editCardId == right.editCardId &&
           left.nodePath == right.nodePath;
}

bool MatchesParameterSubject(const LayoutEditParameterSubject& left, const LayoutEditParameterSubject& right) {
    return left.parameter == right.parameter && MatchesWidgetIdentity(left.widget, right.widget);
}

bool MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right) {
    return left.axis == right.axis && left.renderCardId == right.renderCardId && left.editCardId == right.editCardId &&
           left.nodePath == right.nodePath && left.separatorIndex == right.separatorIndex;
}

bool MatchesGapEditAnchorKey(const LayoutEditGapAnchorKey& left, const LayoutEditGapAnchorKey& right) {
    return MatchesParameterSubject(left, right) && left.nodePath == right.nodePath;
}

bool MatchesEditableAnchorKey(const LayoutEditAnchorKey& left, const LayoutEditAnchorKey& right) {
    return left.anchorId == right.anchorId && MatchesLayoutEditAnchorSubject(left, right) &&
           MatchesWidgetIdentity(left.widget, right.widget);
}

bool MatchesWidgetEditGuide(const LayoutEditWidgetGuide& left, const LayoutEditWidgetGuide& right) {
    return left.axis == right.axis && left.guideId == right.guideId && MatchesParameterSubject(left, right);
}

bool MatchesLayoutContainerEditKey(const LayoutContainerEditKey& left, const LayoutContainerEditKey& right) {
    return left.editCardId == right.editCardId && left.nodePath == right.nodePath;
}

bool MatchesLayoutWeightEditKey(const LayoutWeightEditKey& left, const LayoutWeightEditKey& right) {
    return left.editCardId == right.editCardId && left.nodePath == right.nodePath &&
           left.separatorIndex == right.separatorIndex;
}

bool MatchesLayoutMetricEditKey(const LayoutMetricEditKey& left, const LayoutMetricEditKey& right) {
    return left.metricId == right.metricId;
}

bool MatchesLayoutCardTitleEditKey(const LayoutCardTitleEditKey& left, const LayoutCardTitleEditKey& right) {
    return left.cardId == right.cardId;
}

bool MatchesLayoutMetricListOrderEditKey(
    const LayoutMetricListOrderEditKey& left, const LayoutMetricListOrderEditKey& right) {
    return left.editCardId == right.editCardId && left.nodePath == right.nodePath;
}

bool MatchesCardChromeSelectionIdentity(
    const LayoutEditWidgetIdentity& selection, const LayoutEditWidgetIdentity& candidate) {
    return selection.kind == LayoutEditWidgetIdentity::Kind::CardChrome &&
           candidate.kind == LayoutEditWidgetIdentity::Kind::CardChrome && selection.editCardId == candidate.editCardId;
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& left, const LayoutEditFocusKey& right) {
    if (left.index() != right.index()) {
        return false;
    }
    if (const auto* leftParameter = std::get_if<LayoutEditParameter>(&left)) {
        return *leftParameter == std::get<LayoutEditParameter>(right);
    }
    if (const auto* leftWeight = std::get_if<LayoutWeightEditKey>(&left)) {
        return MatchesLayoutWeightEditKey(*leftWeight, std::get<LayoutWeightEditKey>(right));
    }
    if (const auto* leftMetric = std::get_if<LayoutMetricEditKey>(&left)) {
        return MatchesLayoutMetricEditKey(*leftMetric, std::get<LayoutMetricEditKey>(right));
    }
    if (const auto* leftCardTitle = std::get_if<LayoutCardTitleEditKey>(&left)) {
        return MatchesLayoutCardTitleEditKey(*leftCardTitle, std::get<LayoutCardTitleEditKey>(right));
    }
    if (const auto* leftMetricListOrder = std::get_if<LayoutMetricListOrderEditKey>(&left)) {
        return MatchesLayoutMetricListOrderEditKey(*leftMetricListOrder, std::get<LayoutMetricListOrderEditKey>(right));
    }
    return MatchesLayoutContainerEditKey(
        std::get<LayoutContainerEditKey>(left), std::get<LayoutContainerEditKey>(right));
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditGuide& guide) {
    const auto* weightKey = std::get_if<LayoutWeightEditKey>(&focusKey);
    return weightKey != nullptr && MatchesLayoutWeightEditKey(*weightKey,
                                       LayoutWeightEditKey{guide.editCardId, guide.nodePath, guide.separatorIndex});
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditWidgetGuide& guide) {
    const auto* parameter = std::get_if<LayoutEditParameter>(&focusKey);
    return parameter != nullptr && *parameter == guide.parameter;
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditGapAnchorKey& key) {
    const auto* parameter = std::get_if<LayoutEditParameter>(&focusKey);
    return parameter != nullptr && *parameter == key.parameter;
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditAnchorKey& key) {
    if (const auto* parameter = std::get_if<LayoutEditParameter>(&focusKey)) {
        return key.subject.index() == 0 && *parameter == std::get<LayoutEditParameter>(key.subject);
    }
    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&focusKey)) {
        return key.subject.index() == 1 &&
               MatchesLayoutMetricEditKey(*metricKey, std::get<LayoutMetricEditKey>(key.subject));
    }
    if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&focusKey)) {
        return key.subject.index() == 2 &&
               MatchesLayoutCardTitleEditKey(*cardTitleKey, std::get<LayoutCardTitleEditKey>(key.subject));
    }
    if (const auto* metricListOrderKey = std::get_if<LayoutMetricListOrderEditKey>(&focusKey)) {
        const auto* anchorKey = std::get_if<LayoutMetricListOrderEditKey>(&key.subject);
        return anchorKey != nullptr && MatchesLayoutMetricListOrderEditKey(*metricListOrderKey, *anchorKey);
    }
    const auto* containerKey = std::get_if<LayoutContainerEditKey>(&focusKey);
    const auto* containerOrderKey = std::get_if<LayoutContainerChildOrderEditKey>(&key.subject);
    return containerKey != nullptr && containerOrderKey != nullptr &&
           MatchesLayoutContainerEditKey(
               *containerKey, LayoutContainerEditKey{containerOrderKey->editCardId, containerOrderKey->nodePath});
}

bool MatchesLayoutEditSelectionHighlight(const LayoutEditSelectionHighlight& highlight, const LayoutEditGuide& guide) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    return focusKey != nullptr && MatchesLayoutEditFocusKey(*focusKey, guide);
}

bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditWidgetGuide& guide) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    return focusKey != nullptr && MatchesLayoutEditFocusKey(*focusKey, guide);
}

bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditGapAnchorKey& key) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    return focusKey != nullptr && MatchesLayoutEditFocusKey(*focusKey, key);
}

bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditAnchorKey& key) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    return focusKey != nullptr && MatchesLayoutEditFocusKey(*focusKey, key);
}

bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditColorRegion& region) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    const auto* parameter = focusKey != nullptr ? std::get_if<LayoutEditParameter>(focusKey) : nullptr;
    return parameter != nullptr && *parameter == region.parameter;
}

std::optional<LayoutEditParameter> LayoutEditAnchorParameter(const LayoutEditAnchorKey& key) {
    const auto* parameter = std::get_if<LayoutEditParameter>(&key.subject);
    return parameter != nullptr ? std::optional<LayoutEditParameter>(*parameter) : std::nullopt;
}

std::optional<LayoutMetricEditKey> LayoutEditAnchorMetricKey(const LayoutEditAnchorKey& key) {
    const auto* metricKey = std::get_if<LayoutMetricEditKey>(&key.subject);
    return metricKey != nullptr ? std::optional<LayoutMetricEditKey>(*metricKey) : std::nullopt;
}

std::optional<LayoutCardTitleEditKey> LayoutEditAnchorCardTitleKey(const LayoutEditAnchorKey& key) {
    const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&key.subject);
    return cardTitleKey != nullptr ? std::optional<LayoutCardTitleEditKey>(*cardTitleKey) : std::nullopt;
}

std::optional<LayoutMetricListOrderEditKey> LayoutEditAnchorMetricListOrderKey(const LayoutEditAnchorKey& key) {
    const auto* metricListOrderKey = std::get_if<LayoutMetricListOrderEditKey>(&key.subject);
    return metricListOrderKey != nullptr ? std::optional<LayoutMetricListOrderEditKey>(*metricListOrderKey)
                                         : std::nullopt;
}

std::optional<LayoutContainerChildOrderEditKey> LayoutEditAnchorContainerChildOrderKey(const LayoutEditAnchorKey& key) {
    const auto* containerOrderKey = std::get_if<LayoutContainerChildOrderEditKey>(&key.subject);
    return containerOrderKey != nullptr ? std::optional<LayoutContainerChildOrderEditKey>(*containerOrderKey)
                                        : std::nullopt;
}
