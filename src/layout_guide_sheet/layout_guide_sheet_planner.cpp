#include "layout_guide_sheet/layout_guide_sheet_planner.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>

#include "layout_edit/layout_edit_tooltip_payload.h"
#include "layout_edit/layout_edit_tooltip_text.h"
#include "layout_model/layout_edit_hit_priority.h"
#include "util/utf8.h"

namespace {

std::optional<TooltipPayload> TooltipPayloadFromActiveRegion(const LayoutEditActiveRegion& region) {
    if (const auto* guide = std::get_if<LayoutEditGuide>(&region.payload)) {
        return *guide;
    }
    if (const auto* guide = std::get_if<LayoutEditWidgetGuide>(&region.payload)) {
        return *guide;
    }
    if (const auto* anchor = std::get_if<LayoutEditGapAnchor>(&region.payload)) {
        return *anchor;
    }
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&region.payload)) {
        return *anchor;
    }
    if (const auto* color = std::get_if<LayoutEditColorRegion>(&region.payload)) {
        return *color;
    }
    return std::nullopt;
}

std::pair<std::string, std::string> SplitTooltipLines(const std::wstring& text) {
    const size_t crlf = text.find(L"\r\n");
    const size_t lf = text.find(L'\n');
    const size_t split = crlf != std::wstring::npos ? crlf : lf;
    if (split == std::wstring::npos) {
        return {Utf8FromWide(text), {}};
    }
    const size_t descriptionStart = split + (split == crlf ? 2 : 1);
    return {Utf8FromWide(text.substr(0, split)), Utf8FromWide(text.substr(descriptionStart))};
}

bool RectsOverlap(const RenderRect& lhs, const RenderRect& rhs) {
    return lhs.left < rhs.right && lhs.right > rhs.left && lhs.top < rhs.bottom && lhs.bottom > rhs.top;
}

long long IntersectionArea(const RenderRect& lhs, const RenderRect& rhs) {
    const int left = std::max(lhs.left, rhs.left);
    const int top = std::max(lhs.top, rhs.top);
    const int right = std::min(lhs.right, rhs.right);
    const int bottom = std::min(lhs.bottom, rhs.bottom);
    return static_cast<long long>(std::max(0, right - left)) * static_cast<long long>(std::max(0, bottom - top));
}

struct SourceCardResolution {
    std::string cardId;
    bool overlapsCard = false;
};

bool ContainsCardId(const std::vector<std::string>& cardIds, const std::string& cardId) {
    return std::find(cardIds.begin(), cardIds.end(), cardId) != cardIds.end();
}

bool WidgetIdentityBelongsToSelectedCard(
    const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& selectedCardIds) {
    if (widget.kind != LayoutEditWidgetIdentity::Kind::Widget) {
        return false;
    }
    return ContainsCardId(selectedCardIds, widget.renderCardId);
}

bool PayloadBelongsToSelectedCard(
    const LayoutEditActiveRegionPayload& payload, const std::vector<std::string>& selectedCardIds) {
    if (const auto* guide = std::get_if<LayoutEditGuide>(&payload)) {
        return ContainsCardId(selectedCardIds, guide->renderCardId);
    }
    if (const auto* guide = std::get_if<LayoutEditWidgetGuide>(&payload)) {
        return WidgetIdentityBelongsToSelectedCard(guide->widget, selectedCardIds);
    }
    if (const auto* anchor = std::get_if<LayoutEditGapAnchor>(&payload)) {
        return WidgetIdentityBelongsToSelectedCard(anchor->key.widget, selectedCardIds);
    }
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&payload)) {
        return WidgetIdentityBelongsToSelectedCard(anchor->key.widget, selectedCardIds);
    }
    return true;
}

bool RectOverlapsAnyCardHeader(const RenderRect& rect, const std::vector<LayoutGuideSheetCardSummary>& cards) {
    return std::any_of(cards.begin(), cards.end(), [&](const LayoutGuideSheetCardSummary& card) {
        return card.chromeLayout.hasHeader && RectsOverlap(rect, card.chromeLayout.titleRect);
    });
}

bool IsOverviewPayload(const LayoutEditActiveRegion& region, const std::vector<LayoutGuideSheetCardSummary>& cards) {
    if (const auto* guide = std::get_if<LayoutEditGuide>(&region.payload)) {
        return guide->renderCardId.empty();
    }
    if (const auto* guide = std::get_if<LayoutEditWidgetGuide>(&region.payload)) {
        return guide->widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome;
    }
    if (const auto* anchor = std::get_if<LayoutEditGapAnchor>(&region.payload)) {
        return anchor->key.widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome;
    }
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&region.payload)) {
        return anchor->key.widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome ||
               anchor->key.widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome;
    }
    if (std::holds_alternative<LayoutEditColorRegion>(region.payload)) {
        return RectOverlapsAnyCardHeader(region.box, cards);
    }
    return false;
}

SourceCardResolution ResolveLayoutGuideSheetSourceCard(const LayoutEditActiveRegion& region,
    const std::vector<LayoutGuideSheetCardSummary>& cards,
    const std::vector<std::string>& selectedCardIds) {
    std::string bestCardId;
    long long bestArea = 0;
    for (const LayoutGuideSheetCardSummary& card : cards) {
        const long long area = RectsOverlap(region.box, card.rect) ? IntersectionArea(region.box, card.rect) : 0;
        if (area > bestArea) {
            bestArea = area;
            bestCardId = card.id;
        }
    }
    if (bestArea > 0) {
        return {bestCardId, true};
    }

    long long nearestDistance = (std::numeric_limits<long long>::max)();
    for (const LayoutGuideSheetCardSummary& card : cards) {
        if (!ContainsCardId(selectedCardIds, card.id)) {
            continue;
        }
        const RenderPoint regionCenter = region.box.Center();
        const RenderPoint cardCenter = card.rect.Center();
        const long long dx = static_cast<long long>(regionCenter.x) - static_cast<long long>(cardCenter.x);
        const long long dy = static_cast<long long>(regionCenter.y) - static_cast<long long>(cardCenter.y);
        const long long distance = dx * dx + dy * dy;
        if (distance < nearestDistance) {
            nearestDistance = distance;
            bestCardId = card.id;
        }
    }
    return {bestCardId, false};
}

bool IsRepresentativeWidgetClass(WidgetClass widgetClass) {
    return widgetClass != WidgetClass::Unknown && widgetClass != WidgetClass::NetworkFooter &&
           widgetClass != WidgetClass::VerticalSpacer && widgetClass != WidgetClass::VerticalSpring;
}

std::vector<WidgetClass> UniqueWidgetClasses(const LayoutGuideSheetCardSummary& card) {
    std::vector<WidgetClass> classes;
    for (WidgetClass widgetClass : card.widgetClasses) {
        if (!IsRepresentativeWidgetClass(widgetClass) ||
            std::find(classes.begin(), classes.end(), widgetClass) != classes.end()) {
            continue;
        }
        classes.push_back(widgetClass);
    }
    return classes;
}

size_t CoverageCount(const std::vector<WidgetClass>& covered, const std::vector<WidgetClass>& universe) {
    return static_cast<size_t>(std::count_if(universe.begin(), universe.end(), [&](WidgetClass widgetClass) {
        return std::find(covered.begin(), covered.end(), widgetClass) != covered.end();
    }));
}

void AddCardCoverage(std::vector<WidgetClass>& covered, const LayoutGuideSheetCardSummary& card) {
    for (WidgetClass widgetClass : card.widgetClasses) {
        if (!IsRepresentativeWidgetClass(widgetClass) ||
            std::find(covered.begin(), covered.end(), widgetClass) != covered.end()) {
            continue;
        }
        covered.push_back(widgetClass);
    }
}

std::string LayoutGuideSheetCalloutKey(
    const std::string& parameterLine, const std::string& descriptionLine, const TooltipPayload& payload) {
    if (const std::optional<LayoutEditFocusKey> focusKey = TooltipPayloadFocusKey(payload); focusKey.has_value()) {
        if (std::holds_alternative<LayoutMetricEditKey>(*focusKey)) {
            return "metric_definition";
        }
        if (std::holds_alternative<LayoutCardTitleEditKey>(*focusKey)) {
            return "card_title";
        }
    }
    return parameterLine + "\n" + descriptionLine;
}

void AddOrUpdateCallout(std::vector<LayoutGuideSheetCalloutRequest>& callouts,
    const std::string& key,
    const std::string& sourceCardId,
    const std::string& parameterLine,
    const std::string& descriptionLine,
    const LayoutEditActiveRegion& region,
    const TooltipPayload& payload,
    int priority,
    size_t& order) {
    std::optional<LayoutEditAnchorKey> hoverAnchorKey;
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&region.payload)) {
        hoverAnchorKey = anchor->key;
    }
    std::optional<LayoutEditWidgetGuide> hoverWidgetGuide;
    if (const auto* guide = std::get_if<LayoutEditWidgetGuide>(&region.payload)) {
        hoverWidgetGuide = *guide;
    }
    std::optional<LayoutEditGuide> hoverLayoutGuide;
    if (const auto* guide = std::get_if<LayoutEditGuide>(&region.payload)) {
        hoverLayoutGuide = *guide;
    }
    std::optional<LayoutEditGapAnchorKey> hoverGapAnchorKey;
    if (const auto* anchor = std::get_if<LayoutEditGapAnchor>(&region.payload)) {
        hoverGapAnchorKey = anchor->key;
    }
    std::optional<AnchorShape> hoverAnchorShape;
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&region.payload)) {
        hoverAnchorShape = anchor->shape;
    }

    const auto existing =
        std::find_if(callouts.begin(), callouts.end(), [&](const auto& callout) { return callout.key == key; });
    if (existing != callouts.end()) {
        const bool beforeExisting =
            region.box.top < existing->targetRect.top ||
            (region.box.top == existing->targetRect.top && region.box.left < existing->targetRect.left);
        if (beforeExisting) {
            existing->targetRect = region.box;
            existing->sourceCardId = sourceCardId;
            existing->parameterLine = parameterLine;
            existing->descriptionLine = descriptionLine;
            existing->hoverAnchorKey = hoverAnchorKey;
            existing->hoverWidgetGuide = hoverWidgetGuide;
            existing->hoverLayoutGuide = hoverLayoutGuide;
            existing->hoverGapAnchorKey = hoverGapAnchorKey;
            existing->hoverAnchorShape = hoverAnchorShape;
            existing->priority = priority;
        }
        return;
    }

    callouts.push_back(LayoutGuideSheetCalloutRequest{key,
        sourceCardId,
        parameterLine,
        descriptionLine,
        hoverAnchorKey,
        hoverWidgetGuide,
        hoverLayoutGuide,
        hoverGapAnchorKey,
        hoverAnchorShape,
        region.box,
        priority,
        order++});
    (void)payload;
}

}  // namespace

std::vector<std::string> SelectLayoutGuideSheetCards(const std::vector<LayoutGuideSheetCardSummary>& cards) {
    std::vector<WidgetClass> universe;
    for (const LayoutGuideSheetCardSummary& card : cards) {
        AddCardCoverage(universe, card);
    }
    if (universe.empty()) {
        return cards.empty() ? std::vector<std::string>{} : std::vector<std::string>{cards.front().id};
    }

    std::vector<size_t> bestIndexes;
    size_t bestCoverage = 0;
    size_t bestWidgetCount = 0;
    const size_t combinationCount = cards.size() >= sizeof(size_t) * 8 ? 0 : (size_t{1} << cards.size());
    for (size_t mask = 1; mask < combinationCount; ++mask) {
        std::vector<WidgetClass> covered;
        std::vector<size_t> indexes;
        size_t widgetCount = 0;
        for (size_t i = 0; i < cards.size(); ++i) {
            if ((mask & (size_t{1} << i)) == 0) {
                continue;
            }
            indexes.push_back(i);
            widgetCount += UniqueWidgetClasses(cards[i]).size();
            AddCardCoverage(covered, cards[i]);
        }
        const size_t coverage = CoverageCount(covered, universe);
        const bool better =
            bestIndexes.empty() || coverage > bestCoverage ||
            (coverage == bestCoverage && indexes.size() < bestIndexes.size()) ||
            (coverage == bestCoverage && indexes.size() == bestIndexes.size() && widgetCount > bestWidgetCount);
        if (better) {
            bestIndexes = std::move(indexes);
            bestCoverage = coverage;
            bestWidgetCount = widgetCount;
        }
        if (bestCoverage == universe.size() && bestIndexes.size() == 1) {
            break;
        }
    }

    std::vector<std::string> selected;
    selected.reserve(bestIndexes.size());
    for (size_t index : bestIndexes) {
        selected.push_back(cards[index].id);
    }
    return selected;
}

std::vector<LayoutGuideSheetCalloutRequest> BuildLayoutGuideSheetCallouts(const AppConfig& config,
    const LayoutEditActiveRegions& regions,
    const std::vector<LayoutGuideSheetCardSummary>& cards,
    const std::vector<std::string>& selectedCardIds) {
    std::vector<LayoutGuideSheetCalloutRequest> callouts;
    size_t order = 0;
    for (const LayoutEditActiveRegion& region : regions) {
        const std::optional<TooltipPayload> payload = TooltipPayloadFromActiveRegion(region);
        if (!payload.has_value()) {
            continue;
        }
        std::string tooltipError;
        const auto tooltipText = BuildLayoutEditTooltipTextForPayload(config, *payload, &tooltipError);
        if (!tooltipText.has_value()) {
            continue;
        }
        auto [parameterLine, descriptionLine] = SplitTooltipLines(*tooltipText);
        if (parameterLine.empty()) {
            continue;
        }
        const SourceCardResolution sourceCard = ResolveLayoutGuideSheetSourceCard(region, cards, selectedCardIds);
        if (sourceCard.cardId.empty() || !sourceCard.overlapsCard) {
            continue;
        }
        if (!ContainsCardId(selectedCardIds, sourceCard.cardId)) {
            continue;
        }
        if (!PayloadBelongsToSelectedCard(region.payload, selectedCardIds)) {
            continue;
        }
        const std::string key = LayoutGuideSheetCalloutKey(parameterLine, descriptionLine, *payload);
        const auto parameter = TooltipPayloadParameter(*payload);
        const int priority = parameter.has_value() ? GetLayoutEditParameterHitPriority(*parameter) : 500;
        AddOrUpdateCallout(
            callouts, key, sourceCard.cardId, parameterLine, descriptionLine, region, *payload, priority, order);
    }
    return callouts;
}

std::vector<LayoutGuideSheetCalloutRequest> BuildLayoutGuideSheetOverviewCallouts(const AppConfig& config,
    const LayoutEditActiveRegions& regions,
    const std::vector<LayoutGuideSheetCardSummary>& cards) {
    std::vector<LayoutGuideSheetCalloutRequest> callouts;
    size_t order = 0;
    for (const LayoutEditActiveRegion& region : regions) {
        if (!IsOverviewPayload(region, cards)) {
            continue;
        }
        const std::optional<TooltipPayload> payload = TooltipPayloadFromActiveRegion(region);
        if (!payload.has_value()) {
            continue;
        }
        std::string tooltipError;
        const auto tooltipText = BuildLayoutEditTooltipTextForPayload(config, *payload, &tooltipError);
        if (!tooltipText.has_value()) {
            continue;
        }
        auto [parameterLine, descriptionLine] = SplitTooltipLines(*tooltipText);
        if (parameterLine.empty()) {
            continue;
        }
        const std::string key = LayoutGuideSheetCalloutKey(parameterLine, descriptionLine, *payload);
        const auto parameter = TooltipPayloadParameter(*payload);
        const int priority = parameter.has_value() ? GetLayoutEditParameterHitPriority(*parameter) : 500;
        AddOrUpdateCallout(callouts,
            key,
            kLayoutGuideSheetOverviewSourceId,
            parameterLine,
            descriptionLine,
            region,
            *payload,
            priority,
            order);
    }
    return callouts;
}
