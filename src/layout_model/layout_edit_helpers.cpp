#include "layout_model/layout_edit_helpers.h"

namespace {

bool MatchesLayoutEditAnchorSubject(const LayoutEditAnchorKey& left, const LayoutEditAnchorKey& right) {
    if (left.subject.index() != right.subject.index()) {
        return false;
    }
    if (const auto* leftParameter = std::get_if<LayoutEditParameter>(&left.subject)) {
        const auto* rightParameter = std::get_if<LayoutEditParameter>(&right.subject);
        return rightParameter != nullptr && *leftParameter == *rightParameter;
    }
    if (const auto* leftMetric = std::get_if<LayoutMetricEditKey>(&left.subject)) {
        const auto* rightMetric = std::get_if<LayoutMetricEditKey>(&right.subject);
        return rightMetric != nullptr && MatchesLayoutMetricEditKey(*leftMetric, *rightMetric);
    }
    if (const auto* leftCardTitle = std::get_if<LayoutCardTitleEditKey>(&left.subject)) {
        const auto* rightCardTitle = std::get_if<LayoutCardTitleEditKey>(&right.subject);
        return rightCardTitle != nullptr && MatchesLayoutCardTitleEditKey(*leftCardTitle, *rightCardTitle);
    }
    if (const auto* leftNodeField = std::get_if<LayoutNodeFieldEditKey>(&left.subject)) {
        const auto* rightNodeField = std::get_if<LayoutNodeFieldEditKey>(&right.subject);
        return rightNodeField != nullptr && MatchesLayoutNodeFieldEditKey(*leftNodeField, *rightNodeField);
    }
    const auto* leftContainer = std::get_if<LayoutContainerChildOrderEditKey>(&left.subject);
    const auto* rightContainer = std::get_if<LayoutContainerChildOrderEditKey>(&right.subject);
    if (leftContainer == nullptr || rightContainer == nullptr) {
        return false;
    }
    return MatchesLayoutContainerEditKey(LayoutContainerEditKey{leftContainer->editCardId, leftContainer->nodePath},
        LayoutContainerEditKey{rightContainer->editCardId, rightContainer->nodePath});
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

bool MatchesThemeColorEditKey(const ThemeColorEditKey& left, const ThemeColorEditKey& right) {
    return left.themeName == right.themeName && left.tokenName == right.tokenName;
}

bool MatchesLayoutNodeFieldEditKey(const LayoutNodeFieldEditKey& left, const LayoutNodeFieldEditKey& right) {
    return left.editCardId == right.editCardId && left.nodePath == right.nodePath &&
        left.widgetClass == right.widgetClass && left.field == right.field;
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
        const auto* rightParameter = std::get_if<LayoutEditParameter>(&right);
        return rightParameter != nullptr && *leftParameter == *rightParameter;
    }
    if (const auto* leftWeight = std::get_if<LayoutWeightEditKey>(&left)) {
        const auto* rightWeight = std::get_if<LayoutWeightEditKey>(&right);
        return rightWeight != nullptr && MatchesLayoutWeightEditKey(*leftWeight, *rightWeight);
    }
    if (const auto* leftMetric = std::get_if<LayoutMetricEditKey>(&left)) {
        const auto* rightMetric = std::get_if<LayoutMetricEditKey>(&right);
        return rightMetric != nullptr && MatchesLayoutMetricEditKey(*leftMetric, *rightMetric);
    }
    if (const auto* leftCardTitle = std::get_if<LayoutCardTitleEditKey>(&left)) {
        const auto* rightCardTitle = std::get_if<LayoutCardTitleEditKey>(&right);
        return rightCardTitle != nullptr && MatchesLayoutCardTitleEditKey(*leftCardTitle, *rightCardTitle);
    }
    if (const auto* leftThemeColor = std::get_if<ThemeColorEditKey>(&left)) {
        const auto* rightThemeColor = std::get_if<ThemeColorEditKey>(&right);
        return rightThemeColor != nullptr && MatchesThemeColorEditKey(*leftThemeColor, *rightThemeColor);
    }
    if (const auto* leftNodeField = std::get_if<LayoutNodeFieldEditKey>(&left)) {
        const auto* rightNodeField = std::get_if<LayoutNodeFieldEditKey>(&right);
        return rightNodeField != nullptr && MatchesLayoutNodeFieldEditKey(*leftNodeField, *rightNodeField);
    }
    const auto* leftContainer = std::get_if<LayoutContainerEditKey>(&left);
    const auto* rightContainer = std::get_if<LayoutContainerEditKey>(&right);
    return leftContainer != nullptr && rightContainer != nullptr &&
        MatchesLayoutContainerEditKey(*leftContainer, *rightContainer);
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditGuide& guide) {
    const auto* weightKey = std::get_if<LayoutWeightEditKey>(&focusKey);
    return weightKey != nullptr &&
        MatchesLayoutWeightEditKey(
            *weightKey, LayoutWeightEditKey{guide.editCardId, guide.nodePath, guide.separatorIndex});
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
        const auto* anchorParameter = std::get_if<LayoutEditParameter>(&key.subject);
        return anchorParameter != nullptr && *parameter == *anchorParameter;
    }
    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&focusKey)) {
        const auto* anchorMetricKey = std::get_if<LayoutMetricEditKey>(&key.subject);
        return anchorMetricKey != nullptr && MatchesLayoutMetricEditKey(*metricKey, *anchorMetricKey);
    }
    if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&focusKey)) {
        const auto* anchorCardTitleKey = std::get_if<LayoutCardTitleEditKey>(&key.subject);
        return anchorCardTitleKey != nullptr && MatchesLayoutCardTitleEditKey(*cardTitleKey, *anchorCardTitleKey);
    }
    if (const auto* nodeFieldKey = std::get_if<LayoutNodeFieldEditKey>(&focusKey)) {
        const auto* anchorKey = std::get_if<LayoutNodeFieldEditKey>(&key.subject);
        return anchorKey != nullptr && MatchesLayoutNodeFieldEditKey(*nodeFieldKey, *anchorKey);
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

std::optional<LayoutNodeFieldEditKey> LayoutEditAnchorNodeFieldKey(const LayoutEditAnchorKey& key) {
    const auto* nodeFieldKey = std::get_if<LayoutNodeFieldEditKey>(&key.subject);
    return nodeFieldKey != nullptr ? std::optional<LayoutNodeFieldEditKey>(*nodeFieldKey) : std::nullopt;
}

std::optional<LayoutContainerChildOrderEditKey> LayoutEditAnchorContainerChildOrderKey(const LayoutEditAnchorKey& key) {
    const auto* containerOrderKey = std::get_if<LayoutContainerChildOrderEditKey>(&key.subject);
    return containerOrderKey != nullptr ?
        std::optional<LayoutContainerChildOrderEditKey>(*containerOrderKey) :
        std::nullopt;
}
