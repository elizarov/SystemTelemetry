#include "dashboard_renderer/impl/layout_edit_overlay_renderer.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "dashboard_renderer/dashboard_renderer.h"
#include "dashboard_renderer/impl/layout_resolver.h"
#include "layout_model/layout_edit_anchor_shape.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_parameter_metadata.h"

namespace {

bool SameRect(const RenderRect& left, const RenderRect& right) {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

RenderRect UnionRect(const RenderRect& left, const RenderRect& right) {
    if (left.IsEmpty()) {
        return right;
    }
    if (right.IsEmpty()) {
        return left;
    }
    return RenderRect{
        (std::min)(left.left, right.left),
        (std::min)(left.top, right.top),
        (std::max)(left.right, right.right),
        (std::max)(left.bottom, right.bottom)};
}

RenderRect OffsetRect(RenderRect rect, int dx, int dy) {
    rect.left += dx;
    rect.right += dx;
    rect.top += dy;
    rect.bottom += dy;
    return rect;
}

bool IsFontEditParameter(LayoutEditParameter parameter) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
    return descriptor.has_value() && descriptor->valueFormat == configschema::ValueFormat::FontSpec;
}

bool UseWholeWidgetSelectionOutline(LayoutEditParameter parameter) {
    switch (parameter) {
        case LayoutEditParameter::GaugeOuterPadding:
        case LayoutEditParameter::GaugeRingThickness:
        case LayoutEditParameter::ThroughputGuideStrokeWidth:
        case LayoutEditParameter::ThroughputPlotStrokeWidth:
        case LayoutEditParameter::ThroughputLeaderDiameter:
            return true;
        default:
            return false;
    }
}

bool UseWholeDashboardSelectionOutline(LayoutEditParameter parameter) {
    return parameter == LayoutEditParameter::DashboardOuterMargin ||
        parameter == LayoutEditParameter::DashboardRowGap || parameter == LayoutEditParameter::DashboardColumnGap;
}

bool UseAllCardsSelectionOutline(LayoutEditParameter parameter) {
    return parameter == LayoutEditParameter::CardRowGap || parameter == LayoutEditParameter::CardColumnGap;
}

bool SuppressAnchorTargetSelectionOutline(LayoutEditParameter parameter) {
    return parameter == LayoutEditParameter::GaugeOuterPadding || parameter == LayoutEditParameter::GaugeRingThickness;
}

RenderColorId ActiveEditColor(const DashboardOverlayState& overlayState) {
    return overlayState.forceHoverEquivalentAffordances ? RenderColorId::LayoutGuide : RenderColorId::ActiveEdit;
}

bool UseActiveEditEmphasis(const DashboardOverlayState& overlayState, bool active) {
    return active && !overlayState.forceHoverEquivalentAffordances;
}

struct SimilarityTypeKey {
    WidgetClass widgetClass = WidgetClass::Unknown;
    int extent = 0;

    bool operator==(const SimilarityTypeKey& other) const {
        return widgetClass == other.widgetClass && extent == other.extent;
    }
};

struct VisibleWidgetEntry {
    const WidgetLayout* widget = nullptr;
    SimilarityTypeKey type;
    bool hasExactType = false;
    int exactTypeOrdinal = 0;
};

std::vector<VisibleWidgetEntry>::iterator FindVisibleWidgetEntry(
    std::vector<VisibleWidgetEntry>& entries, const WidgetLayout* widget) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->widget == widget) {
            return it;
        }
    }
    return entries.end();
}

}  // namespace

std::vector<LayoutEditAnchorRegion> CollectRelatedEditableAnchorHighlights(
    const std::vector<LayoutEditAnchorRegion>& staticRegions,
    const std::vector<LayoutEditAnchorRegion>& dynamicRegions,
    const LayoutEditAnchorRegion& source) {
    std::vector<LayoutEditAnchorRegion> highlights;
    const auto collect = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
        for (const auto& region : regions) {
            if (!::MatchesWidgetIdentity(region.key.widget, source.key.widget) ||
                !SameRect(region.targetRect, source.targetRect)) {
                continue;
            }
            highlights.push_back(region);
        }
    };
    collect(staticRegions);
    collect(dynamicRegions);
    return highlights;
}

DashboardLayoutEditOverlayRenderer::DashboardLayoutEditOverlayRenderer(
    DashboardRenderer& renderer, DashboardLayoutResolver& layoutResolver)
    : renderer_(renderer), layoutResolver_(layoutResolver) {}

void DashboardLayoutEditOverlayRenderer::Draw(const DashboardOverlayState& overlayState, const MetricSource& metrics) {
    DrawBackgroundAffordances(overlayState);
    DrawDraggedContent(metrics);
    DrawForegroundAffordances(overlayState);
}

void DashboardLayoutEditOverlayRenderer::DrawBackgroundAffordances(const DashboardOverlayState& overlayState) const {
    DrawAffordances(overlayState, LayoutEditOverlayAffordanceLayer::Background);
}

void DashboardLayoutEditOverlayRenderer::DrawDraggedContent(const MetricSource& metrics) {
    DrawContainerChildReorderOverlay(metrics);
}

void DashboardLayoutEditOverlayRenderer::DrawForegroundAffordances(const DashboardOverlayState& overlayState) const {
    if (!HasForegroundAffordanceLayer(overlayState)) {
        return;
    }
    DrawAffordances(overlayState, LayoutEditOverlayAffordanceLayer::Foreground);
}

void DashboardLayoutEditOverlayRenderer::DrawAffordances(
    const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const {
    DrawSelectedColorEditHighlights(overlayState, layer);
    DrawSelectedTreeNodeHighlight(overlayState, layer);
    DrawHoveredWidgetHighlight(overlayState, layer);
    DrawHoveredEditableAnchorHighlight(overlayState, layer);
    DrawLayoutEditGuides(overlayState, layer);
    DrawGapEditAnchors(overlayState, layer);
    DrawWidgetEditGuides(overlayState, layer);
    DrawLayoutSimilarityIndicators(overlayState, layer);
}

void DashboardLayoutEditOverlayRenderer::DrawHoveredWidgetHighlight(
    const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawLayoutEditAffordances()) {
        return;
    }
    const std::optional<OverlayAffordanceRect> hoveredRect = FindHoveredWidgetOutlineRect(overlayState);
    if (!hoveredRect.has_value() || hoveredRect->rect.IsEmpty()) {
        return;
    }
    if (!ShouldDrawAffordanceLayer(hoveredRect->layer, layer)) {
        return;
    }
    const std::vector<LayoutEditOverlayOwner> noOwners;
    const std::vector<LayoutEditOverlayOwner>& owners =
        hoveredRect->owners != nullptr ? *hoveredRect->owners : noOwners;
    renderer_.Renderer().DrawSolidRect(
        ApplyOverlayDragOffset(overlayState, hoveredRect->rect, owners),
        RenderStroke::Solid(RenderColorId::LayoutGuide));
}

void DashboardLayoutEditOverlayRenderer::DrawHoveredEditableAnchorHighlight(
    const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawLayoutEditAffordances()) {
        return;
    }

    std::vector<std::pair<LayoutEditAnchorRegion, bool>> highlights;
    const auto appendHighlight = [&](const LayoutEditAnchorRegion& region, bool active) {
        const auto existing = std::find_if(highlights.begin(), highlights.end(), [&](const auto& entry) {
            return MatchesEditableAnchorKey(entry.first.key, region.key);
        });
        if (existing == highlights.end()) {
            highlights.push_back({region, active});
            return;
        }
        existing->second = existing->second || active;
    };
    const auto appendRelatedHighlights = [&](const LayoutEditAnchorRegion& source, bool active) {
        for (const auto& region : CollectRelatedEditableAnchorHighlights(
                 layoutResolver_.staticEditableAnchorRegions_, layoutResolver_.dynamicEditableAnchorRegions_, source)) {
            appendHighlight(region, active && MatchesEditableAnchorKey(region.key, source.key));
        }
    };
    const auto appendByKey = [&](const std::optional<LayoutEditAnchorKey>& key, bool active) {
        if (!key.has_value()) {
            return;
        }
        const LayoutEditAnchorRegion* region = renderer_.FindEditableAnchorRegion(*key);
        if (region != nullptr) {
            appendRelatedHighlights(*region, active);
        }
    };
    if (overlayState.selectedTreeHighlight.has_value()) {
        const auto collectSelected = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                const auto* special =
                    std::get_if<LayoutEditSelectionHighlightSpecial>(&*overlayState.selectedTreeHighlight);
                if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, region.key) ||
                    (special != nullptr && *special == LayoutEditSelectionHighlightSpecial::AllTexts &&
                     LayoutEditAnchorParameter(region.key).has_value() &&
                     IsFontEditParameter(*LayoutEditAnchorParameter(region.key)))) {
                    appendHighlight(region, true);
                }
            }
        };
        collectSelected(layoutResolver_.staticEditableAnchorRegions_);
        collectSelected(layoutResolver_.dynamicEditableAnchorRegions_);
    }
    if (overlayState.hoveredEditableWidget.has_value()) {
        const auto collectHovered = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (!region.showWhenWidgetHovered) {
                    continue;
                }
                if (region.key.widget.kind != LayoutEditWidgetIdentity::Kind::Widget ||
                    region.key.widget.renderCardId != overlayState.hoveredEditableWidget->renderCardId ||
                    region.key.widget.editCardId != overlayState.hoveredEditableWidget->editCardId ||
                    region.key.widget.nodePath != overlayState.hoveredEditableWidget->nodePath) {
                    continue;
                }
                appendHighlight(region, false);
            }
        };
        collectHovered(layoutResolver_.staticEditableAnchorRegions_);
        collectHovered(layoutResolver_.dynamicEditableAnchorRegions_);
    }
    if (overlayState.hoveredEditableCard.has_value()) {
        const auto collectHoveredCard = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (!region.showWhenWidgetHovered ||
                    region.key.widget.kind != LayoutEditWidgetIdentity::Kind::CardChrome ||
                    region.key.widget.renderCardId != overlayState.hoveredEditableCard->renderCardId ||
                    region.key.widget.editCardId != overlayState.hoveredEditableCard->editCardId) {
                    continue;
                }
                appendHighlight(region, false);
            }
        };
        collectHoveredCard(layoutResolver_.staticEditableAnchorRegions_);
        collectHoveredCard(layoutResolver_.dynamicEditableAnchorRegions_);
    }
    if (overlayState.hoverOnExposedDashboard && overlayState.drawExposedDashboardChrome) {
        const auto collectHoveredDashboard = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (!region.showWhenWidgetHovered ||
                    region.key.widget.kind != LayoutEditWidgetIdentity::Kind::DashboardChrome) {
                    continue;
                }
                appendHighlight(region, false);
            }
        };
        collectHoveredDashboard(layoutResolver_.staticEditableAnchorRegions_);
        collectHoveredDashboard(layoutResolver_.dynamicEditableAnchorRegions_);
    }
    appendByKey(overlayState.hoveredEditableAnchor, false);
    appendByKey(overlayState.activeEditableAnchor, true);
    if (highlights.empty()) {
        return;
    }
    std::optional<RenderRect> hoveredWidgetOutlineRect;
    if (const auto hoveredOutline = FindHoveredWidgetOutlineRect(overlayState);
        hoveredOutline.has_value() && ShouldDrawAffordanceLayer(hoveredOutline->layer, layer)) {
        const std::vector<LayoutEditOverlayOwner> noOwners;
        const std::vector<LayoutEditOverlayOwner>& owners =
            hoveredOutline->owners != nullptr ? *hoveredOutline->owners : noOwners;
        hoveredWidgetOutlineRect = ApplyOverlayDragOffset(overlayState, hoveredOutline->rect, owners);
    }
    for (const auto& highlight : highlights) {
        LayoutEditAnchorRegion highlighted = highlight.first;
        const bool active = highlight.second;
        if (!ShouldDrawAffordanceLayer(highlighted.overlayLayer, layer)) {
            continue;
        }
        ApplyOverlayDragOffset(overlayState, highlighted);
        const RenderColorId outlineColor = active ? ActiveEditColor(overlayState) : RenderColorId::LayoutGuide;
        const bool activeEmphasis = UseActiveEditEmphasis(overlayState, active);
        bool drawTargetOutline = highlighted.drawTargetOutline && !highlighted.targetRect.IsEmpty();
        if (!active && drawTargetOutline && hoveredWidgetOutlineRect.has_value() &&
            SameRect(highlighted.targetRect, *hoveredWidgetOutlineRect)) {
            drawTargetOutline = false;
        }
        if (drawTargetOutline) {
            DrawDottedHighlightRect(highlighted.targetRect, outlineColor, activeEmphasis);
        }

        const bool reorderShape =
            highlighted.shape == AnchorShape::VerticalReorder || highlighted.shape == AnchorShape::HorizontalReorder;
        const int outlineWidth = reorderShape
            ? (std::max)(1, renderer_.ScaleLogical(1))
            : (activeEmphasis ? (std::max)(2, renderer_.ScaleLogical(2)) : (std::max)(1, renderer_.ScaleLogical(1)));
        DrawLayoutEditAnchorShape(
            renderer_.Renderer(),
            highlighted.shape,
            highlighted.anchorRect,
            outlineColor,
            static_cast<float>(outlineWidth),
            (std::max)(1, renderer_.ScaleLogical(1)),
            true,
            true);
    }
}

std::optional<DashboardLayoutEditOverlayRenderer::OverlayAffordanceRect> DashboardLayoutEditOverlayRenderer::
    FindHoveredWidgetOutlineRect(const DashboardOverlayState& overlayState) const {
    if (overlayState.hoveredEditableWidget.has_value() &&
        overlayState.hoveredEditableWidget->kind == LayoutEditWidgetIdentity::Kind::Widget) {
        for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
            for (const auto& widget : card.widgets) {
                if (renderer_.MatchesWidgetIdentity(widget, *overlayState.hoveredEditableWidget)) {
                    return OverlayAffordanceRect{widget.rect, &widget.overlayOwners, widget.overlayLayer};
                }
            }
        }
    }

    if (overlayState.hoveredEditableCard.has_value() &&
        overlayState.hoveredEditableCard->kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
        for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
            const LayoutEditWidgetIdentity cardIdentity{
                card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
            if (MatchesCardChromeSelectionIdentity(*overlayState.hoveredEditableCard, cardIdentity) &&
                !card.chromeLayout.titleRect.IsEmpty()) {
                const RenderRect titleRect = card.chromeLayout.iconRect.IsEmpty()
                    ? card.chromeLayout.titleRect
                    : UnionRect(card.chromeLayout.iconRect, card.chromeLayout.titleRect);
                return OverlayAffordanceRect{titleRect, &card.chrome.overlayOwners, card.chrome.overlayLayer};
            }
        }
    }

    return std::nullopt;
}

void DashboardLayoutEditOverlayRenderer::DrawSelectedColorEditHighlights(
    const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawSelectedTreeHighlight()) {
        return;
    }

    std::vector<RenderRect> highlightedRects;
    const auto appendRect = [&](const LayoutEditColorRegion& region) {
        if (region.targetRect.IsEmpty()) {
            return;
        }
        if (!ShouldDrawAffordanceLayer(region.overlayLayer, layer)) {
            return;
        }
        const RenderRect adjustedRect = ApplyOverlayDragOffset(overlayState, region.targetRect, region.overlayOwners);
        const auto existing =
            std::find_if(highlightedRects.begin(), highlightedRects.end(), [&](const RenderRect& candidate) {
                return candidate.left == adjustedRect.left && candidate.top == adjustedRect.top &&
                    candidate.right == adjustedRect.right && candidate.bottom == adjustedRect.bottom;
            });
        if (existing == highlightedRects.end()) {
            highlightedRects.push_back(adjustedRect);
        }
    };
    const auto collect = [&](const std::vector<LayoutEditColorRegion>& regions) {
        for (const auto& region : regions) {
            if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, region)) {
                appendRect(region);
            }
        }
    };
    collect(layoutResolver_.staticColorEditRegions_);
    collect(layoutResolver_.dynamicColorEditRegions_);
    for (const auto& rect : highlightedRects) {
        DrawDottedHighlightRect(rect, ActiveEditColor(overlayState), !overlayState.forceHoverEquivalentAffordances);
    }
}

void DashboardLayoutEditOverlayRenderer::DrawSelectedTreeNodeHighlight(
    const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawSelectedTreeHighlight()) {
        return;
    }

    const RenderColorId color = ActiveEditColor(overlayState);
    const bool activeEmphasis = !overlayState.forceHoverEquivalentAffordances;
    const std::vector<LayoutEditOverlayOwner> noOwners;
    std::vector<RenderRect> selectedRects;
    bool drawDashboardBoundsOutline = false;
    const auto appendRect = [&](const RenderRect& rect,
                                const std::vector<LayoutEditOverlayOwner>& owners,
                                LayoutEditOverlayAffordanceLayer artifactLayer) {
        if (rect.IsEmpty()) {
            return;
        }
        if (!ShouldDrawAffordanceLayer(artifactLayer, layer)) {
            return;
        }
        const RenderRect adjustedRect = ApplyOverlayDragOffset(overlayState, rect, owners);
        const auto existing =
            std::find_if(selectedRects.begin(), selectedRects.end(), [&](const RenderRect& candidate) {
                return candidate.left == adjustedRect.left && candidate.top == adjustedRect.top &&
                    candidate.right == adjustedRect.right && candidate.bottom == adjustedRect.bottom;
            });
        if (existing == selectedRects.end()) {
            selectedRects.push_back(adjustedRect);
        }
    };
    const auto appendWidgetRectsForIdentity = [&](const LayoutEditWidgetIdentity& identity) {
        for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
            for (const auto& widget : card.widgets) {
                const LayoutEditWidgetIdentity candidateIdentity{widget.cardId, widget.editCardId, widget.nodePath};
                if (::MatchesWidgetIdentity(identity, candidateIdentity)) {
                    appendRect(widget.rect, widget.overlayOwners, widget.overlayLayer);
                }
            }
        }
    };
    if (const auto* focusKey = std::get_if<LayoutEditFocusKey>(&*overlayState.selectedTreeHighlight)) {
        if (const auto* parameter = std::get_if<LayoutEditParameter>(focusKey)) {
            if (UseWholeDashboardSelectionOutline(*parameter)) {
                drawDashboardBoundsOutline = true;
            }
            if (UseAllCardsSelectionOutline(*parameter)) {
                for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
                    appendRect(card.rect, card.chrome.overlayOwners, card.chrome.overlayLayer);
                }
            }
        }
    }
    for (const auto& guide : layoutResolver_.layoutEditGuides_) {
        const bool matchesFocus = MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide);
        const auto* containerKey = std::get_if<LayoutContainerEditKey>(&*overlayState.selectedTreeHighlight);
        const bool matchesContainer =
            containerKey != nullptr && MatchesLayoutContainerEditKey(*containerKey, {guide.editCardId, guide.nodePath});
        if (matchesFocus || matchesContainer) {
            appendRect(guide.containerRect, guide.overlayOwners, guide.overlayLayer);
        }
    }
    for (const auto& guide : layoutResolver_.widgetEditGuides_) {
        if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide)) {
            appendRect(guide.widgetRect, guide.overlayOwners, guide.overlayLayer);
        }
    }
    const auto collectAnchorTargets = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
        for (const auto& region : regions) {
            const auto* special =
                std::get_if<LayoutEditSelectionHighlightSpecial>(&*overlayState.selectedTreeHighlight);
            if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, region.key) ||
                (special != nullptr && *special == LayoutEditSelectionHighlightSpecial::AllTexts &&
                 LayoutEditAnchorParameter(region.key).has_value() &&
                 IsFontEditParameter(*LayoutEditAnchorParameter(region.key)))) {
                if (const auto parameter = LayoutEditAnchorParameter(region.key); parameter.has_value()) {
                    if (!SuppressAnchorTargetSelectionOutline(*parameter)) {
                        appendRect(region.targetRect, region.overlayOwners, region.overlayLayer);
                    }
                    if (UseWholeWidgetSelectionOutline(*parameter)) {
                        appendWidgetRectsForIdentity(region.key.widget);
                    }
                } else {
                    appendRect(region.targetRect, region.overlayOwners, region.overlayLayer);
                }
            }
        }
    };
    collectAnchorTargets(layoutResolver_.staticEditableAnchorRegions_);
    collectAnchorTargets(layoutResolver_.dynamicEditableAnchorRegions_);
    for (const auto& rect : selectedRects) {
        DrawDottedHighlightRect(rect, color, activeEmphasis);
    }
    if (drawDashboardBoundsOutline) {
        DrawDottedAffordanceRect(
            overlayState,
            layer,
            RenderRect{0, 0, layoutResolver_.resolvedLayout_.windowWidth, layoutResolver_.resolvedLayout_.windowHeight},
            noOwners,
            LayoutEditOverlayAffordanceLayer::Background,
            color,
            activeEmphasis,
            false);
    }

    if (const auto* widgetClass = std::get_if<WidgetClass>(&*overlayState.selectedTreeHighlight)) {
        for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
            for (const auto& widget : card.widgets) {
                if (widget.widget != nullptr && widget.widgetClass == *widgetClass) {
                    DrawDottedAffordanceRect(
                        overlayState,
                        layer,
                        widget.rect,
                        widget.overlayOwners,
                        widget.overlayLayer,
                        color,
                        activeEmphasis);
                }
            }
        }
        return;
    }

    if (const auto* widgetIdentity = std::get_if<LayoutEditWidgetIdentity>(&*overlayState.selectedTreeHighlight)) {
        if (widgetIdentity->kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            std::vector<OverlayAffordanceRect> embeddedInstanceRects;
            const auto appendEmbeddedRect = [&](const RenderRect& rect,
                                                const std::vector<LayoutEditOverlayOwner>* owners,
                                                LayoutEditOverlayAffordanceLayer artifactLayer) {
                if (rect.IsEmpty()) {
                    return;
                }
                const auto existing = std::find_if(
                    embeddedInstanceRects.begin(), embeddedInstanceRects.end(), [&](const auto& candidate) {
                        return candidate.rect.left == rect.left && candidate.rect.top == rect.top &&
                            candidate.rect.right == rect.right && candidate.rect.bottom == rect.bottom &&
                            candidate.layer == artifactLayer;
                    });
                if (existing == embeddedInstanceRects.end()) {
                    embeddedInstanceRects.push_back(OverlayAffordanceRect{rect, owners, artifactLayer});
                }
            };
            for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
                const LayoutEditWidgetIdentity cardIdentity{
                    card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
                if (MatchesCardChromeSelectionIdentity(*widgetIdentity, cardIdentity)) {
                    DrawDottedAffordanceRect(
                        overlayState,
                        layer,
                        card.rect,
                        card.chrome.overlayOwners,
                        card.chrome.overlayLayer,
                        color,
                        activeEmphasis);
                }
            }
            for (const auto& guide : layoutResolver_.layoutEditGuides_) {
                const LayoutEditWidgetIdentity cardIdentity{
                    guide.renderCardId, guide.editCardId, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
                if (guide.renderCardId.empty() || guide.renderCardId == guide.editCardId || !guide.nodePath.empty() ||
                    !MatchesCardChromeSelectionIdentity(*widgetIdentity, cardIdentity)) {
                    continue;
                }
                appendEmbeddedRect(guide.containerRect, &guide.overlayOwners, guide.overlayLayer);
            }
            if (embeddedInstanceRects.empty()) {
                for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
                    RenderRect embeddedBounds{};
                    const std::vector<LayoutEditOverlayOwner>* embeddedOwners = nullptr;
                    LayoutEditOverlayAffordanceLayer embeddedLayer = LayoutEditOverlayAffordanceLayer::Background;
                    for (const auto& widget : card.widgets) {
                        const LayoutEditWidgetIdentity cardIdentity{
                            widget.cardId, widget.editCardId, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
                        if (widget.cardId == widget.editCardId ||
                            !MatchesCardChromeSelectionIdentity(*widgetIdentity, cardIdentity)) {
                            continue;
                        }
                        embeddedBounds = UnionRect(embeddedBounds, widget.rect);
                        if (embeddedOwners == nullptr ||
                            widget.overlayLayer == LayoutEditOverlayAffordanceLayer::Foreground) {
                            embeddedOwners = &widget.overlayOwners;
                            embeddedLayer = widget.overlayLayer;
                        }
                    }
                    appendEmbeddedRect(embeddedBounds, embeddedOwners, embeddedLayer);
                }
            }
            for (const auto& artifact : embeddedInstanceRects) {
                const std::vector<LayoutEditOverlayOwner>& owners =
                    artifact.owners != nullptr ? *artifact.owners : noOwners;
                DrawDottedAffordanceRect(
                    overlayState, layer, artifact.rect, owners, artifact.layer, color, activeEmphasis);
            }
            return;
        }
    }

    if (const auto* special = std::get_if<LayoutEditSelectionHighlightSpecial>(&*overlayState.selectedTreeHighlight)) {
        if (*special == LayoutEditSelectionHighlightSpecial::AllCards) {
            for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
                DrawDottedAffordanceRect(
                    overlayState,
                    layer,
                    card.rect,
                    card.chrome.overlayOwners,
                    card.chrome.overlayLayer,
                    color,
                    activeEmphasis);
            }
            return;
        }
        if (*special == LayoutEditSelectionHighlightSpecial::AllTexts) {
            for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
                for (const auto& widget : card.widgets) {
                    if (widget.widget != nullptr && widget.widgetClass == WidgetClass::Text) {
                        DrawDottedAffordanceRect(
                            overlayState,
                            layer,
                            widget.rect,
                            widget.overlayOwners,
                            widget.overlayLayer,
                            color,
                            activeEmphasis);
                    }
                }
            }
        }
        if (*special == LayoutEditSelectionHighlightSpecial::DashboardBounds) {
            DrawDottedAffordanceRect(
                overlayState,
                layer,
                RenderRect{
                    0, 0, layoutResolver_.resolvedLayout_.windowWidth, layoutResolver_.resolvedLayout_.windowHeight},
                noOwners,
                LayoutEditOverlayAffordanceLayer::Background,
                color,
                activeEmphasis,
                false);
            return;
        }
    }
}

void DashboardLayoutEditOverlayRenderer::DrawLayoutEditGuides(
    const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const {
    if (!overlayState.ShouldDrawLayoutEditAffordances() || layoutResolver_.layoutEditGuides_.empty()) {
        return;
    }

    std::vector<std::pair<RenderRect, bool>> containerHighlights;
    const auto appendContainerHighlight = [&](const LayoutEditGuide& guide, bool active) {
        const RenderRect& rect = guide.containerRect;
        if (rect.IsEmpty()) {
            return;
        }
        if (!ShouldDrawAffordanceLayer(guide.overlayLayer, layer)) {
            return;
        }
        const RenderRect adjustedRect = ApplyOverlayDragOffset(overlayState, rect, guide.overlayOwners);
        const auto existing =
            std::find_if(containerHighlights.begin(), containerHighlights.end(), [&](const auto& entry) {
                return entry.first.left == adjustedRect.left && entry.first.top == adjustedRect.top &&
                    entry.first.right == adjustedRect.right && entry.first.bottom == adjustedRect.bottom;
            });
        if (existing == containerHighlights.end()) {
            containerHighlights.push_back({adjustedRect, active});
            return;
        }
        existing->second = existing->second || active;
    };
    if (!overlayState.suppressLayoutGuideContainerHighlights) {
        if (overlayState.hoveredLayoutEditGuide.has_value()) {
            appendContainerHighlight(*overlayState.hoveredLayoutEditGuide, false);
        }
        if (overlayState.activeLayoutEditGuide.has_value()) {
            appendContainerHighlight(*overlayState.activeLayoutEditGuide, true);
        }
        for (const auto& [rect, active] : containerHighlights) {
            const RenderColorId color = active ? ActiveEditColor(overlayState) : RenderColorId::LayoutGuide;
            DrawDottedHighlightRect(rect, color, UseActiveEditEmphasis(overlayState, active));
        }
    }

    const int lineWidth = (std::max)(1, renderer_.ScaleLogical(1));
    const int activeLineWidth = (std::max)(lineWidth + 1, renderer_.ScaleLogical(2));
    for (const auto& guide : layoutResolver_.layoutEditGuides_) {
        const bool active = overlayState.activeLayoutEditGuide.has_value() &&
            MatchesLayoutEditGuide(guide, *overlayState.activeLayoutEditGuide);
        const bool hoveredGuide = overlayState.hoveredLayoutEditGuide.has_value() &&
            MatchesLayoutEditGuide(guide, *overlayState.hoveredLayoutEditGuide);
        const bool selected = overlayState.selectedTreeHighlight.has_value() &&
            MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide);
        const bool emphasized = active || selected;
        if (!emphasized && !hoveredGuide && !overlayState.hoverOnExposedDashboard) {
            continue;
        }
        if (!ShouldDrawAffordanceLayer(guide.overlayLayer, layer)) {
            continue;
        }
        const RenderColorId color = emphasized ? ActiveEditColor(overlayState) : RenderColorId::LayoutGuide;
        const RenderPoint start = ApplyOverlayDragOffset(
            overlayState, RenderPoint{guide.lineRect.left, guide.lineRect.top}, guide.overlayOwners);
        const RenderPoint end = ApplyOverlayDragOffset(
            overlayState,
            guide.axis == LayoutGuideAxis::Vertical ? RenderPoint{guide.lineRect.left, guide.lineRect.bottom}
                                                    : RenderPoint{guide.lineRect.right, guide.lineRect.top},
            guide.overlayOwners);
        renderer_.Renderer().DrawSolidLine(
            start,
            end,
            RenderStroke::Solid(
                color,
                static_cast<float>(UseActiveEditEmphasis(overlayState, emphasized) ? activeLineWidth : lineWidth)));
    }
}

void DashboardLayoutEditOverlayRenderer::DrawWidgetEditGuides(
    const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawLayoutEditAffordances() || layoutResolver_.widgetEditGuides_.empty()) {
        return;
    }

    const auto shouldDraw = [&](const LayoutEditWidgetGuide& guide) {
        if (overlayState.activeWidgetEditGuide.has_value()) {
            return guide.widget.kind == overlayState.activeWidgetEditGuide->widget.kind &&
                guide.widget.renderCardId == overlayState.activeWidgetEditGuide->widget.renderCardId &&
                guide.widget.editCardId == overlayState.activeWidgetEditGuide->widget.editCardId &&
                guide.widget.nodePath == overlayState.activeWidgetEditGuide->widget.nodePath;
        }
        if (overlayState.selectedTreeHighlight.has_value() &&
            MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide)) {
            return true;
        }
        if (guide.widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            return overlayState.hoveredEditableCard.has_value() &&
                guide.widget.renderCardId == overlayState.hoveredEditableCard->renderCardId &&
                guide.widget.editCardId == overlayState.hoveredEditableCard->editCardId;
        }
        if (!overlayState.hoveredEditableWidget.has_value()) {
            return false;
        }
        return guide.widget.kind == LayoutEditWidgetIdentity::Kind::Widget &&
            guide.widget.renderCardId == overlayState.hoveredEditableWidget->renderCardId &&
            guide.widget.editCardId == overlayState.hoveredEditableWidget->editCardId &&
            guide.widget.nodePath == overlayState.hoveredEditableWidget->nodePath;
    };

    const int lineWidth = (std::max)(1, renderer_.ScaleLogical(1));
    for (const auto& guide : layoutResolver_.widgetEditGuides_) {
        if (!shouldDraw(guide)) {
            continue;
        }
        if (!ShouldDrawAffordanceLayer(guide.overlayLayer, layer)) {
            continue;
        }
        const bool active = overlayState.activeWidgetEditGuide.has_value() &&
            MatchesWidgetEditGuide(guide, *overlayState.activeWidgetEditGuide);
        const bool selected = !overlayState.activeWidgetEditGuide.has_value() &&
            overlayState.selectedTreeHighlight.has_value() &&
            MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide);
        const bool emphasized = active || selected;
        const RenderColorId color = emphasized ? ActiveEditColor(overlayState) : RenderColorId::LayoutGuide;
        renderer_.Renderer().DrawSolidLine(
            ApplyOverlayDragOffset(overlayState, guide.drawStart, guide.overlayOwners),
            ApplyOverlayDragOffset(overlayState, guide.drawEnd, guide.overlayOwners),
            RenderStroke::Solid(color, static_cast<float>(lineWidth)));
    }
}

void DashboardLayoutEditOverlayRenderer::DrawGapEditAnchors(
    const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawLayoutEditAffordances() || layoutResolver_.gapEditAnchors_.empty()) {
        return;
    }

    const auto shouldDraw = [&](const LayoutEditGapAnchor& anchor) {
        if (overlayState.activeGapEditAnchor.has_value()) {
            if (anchor.key.widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome) {
                return overlayState.activeGapEditAnchor->widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome;
            }
            return overlayState.activeGapEditAnchor->widget.kind == LayoutEditWidgetIdentity::Kind::CardChrome &&
                anchor.key.widget.renderCardId == overlayState.activeGapEditAnchor->widget.renderCardId &&
                anchor.key.widget.editCardId == overlayState.activeGapEditAnchor->widget.editCardId;
        }
        const bool selected = overlayState.selectedTreeHighlight.has_value() &&
            MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, anchor.key);
        if (selected) {
            return true;
        }
        if (overlayState.hoveredGapEditAnchor.has_value() &&
            MatchesGapEditAnchorKey(anchor.key, *overlayState.hoveredGapEditAnchor)) {
            return true;
        }
        if (overlayState.selectedTreeHighlight.has_value() && !overlayState.hoverOnExposedDashboard &&
            !overlayState.hoveredLayoutCard.has_value() && !overlayState.hoveredGapEditAnchor.has_value()) {
            return false;
        }
        if (overlayState.hoverOnExposedDashboard) {
            return true;
        }
        if (anchor.key.widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome) {
            return overlayState.drawExposedDashboardChrome;
        }
        return overlayState.hoveredLayoutCard.has_value() &&
            anchor.key.widget.renderCardId == overlayState.hoveredLayoutCard->renderCardId &&
            anchor.key.widget.editCardId == overlayState.hoveredLayoutCard->editCardId;
    };

    const int capHalf = (std::max)(2, renderer_.ScaleLogical(4));
    const int lineWidth = (std::max)(1, renderer_.ScaleLogical(1));
    const int handleOutline = (std::max)(1, renderer_.ScaleLogical(1));
    for (const auto& anchor : layoutResolver_.gapEditAnchors_) {
        if (!shouldDraw(anchor)) {
            continue;
        }
        const bool active = overlayState.activeGapEditAnchor.has_value() &&
            MatchesGapEditAnchorKey(anchor.key, *overlayState.activeGapEditAnchor);
        const bool selected = !overlayState.activeGapEditAnchor.has_value() &&
            overlayState.selectedTreeHighlight.has_value() &&
            MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, anchor.key);
        const bool hovered = overlayState.hoveredGapEditAnchor.has_value() &&
            MatchesGapEditAnchorKey(anchor.key, *overlayState.hoveredGapEditAnchor);
        const bool emphasized = active || selected;
        const RenderColorId color = emphasized ? ActiveEditColor(overlayState) : RenderColorId::LayoutGuide;
        const float strokeWidth = static_cast<float>(lineWidth);
        if (!ShouldDrawAffordanceLayer(anchor.overlayLayer, layer)) {
            continue;
        }
        const RenderPoint drawStart = ApplyOverlayDragOffset(overlayState, anchor.drawStart, anchor.overlayOwners);
        const RenderPoint drawEnd = ApplyOverlayDragOffset(overlayState, anchor.drawEnd, anchor.overlayOwners);
        const RenderRect handleRect = ApplyOverlayDragOffset(overlayState, anchor.handleRect, anchor.overlayOwners);

        renderer_.Renderer().DrawSolidLine(drawStart, drawEnd, RenderStroke::Solid(color, strokeWidth));
        if (anchor.axis == LayoutGuideAxis::Vertical) {
            renderer_.Renderer().DrawSolidLine(
                RenderPoint{drawStart.x - capHalf, drawStart.y},
                RenderPoint{drawStart.x + capHalf, drawStart.y},
                RenderStroke::Solid(color, strokeWidth));
            renderer_.Renderer().DrawSolidLine(
                RenderPoint{drawEnd.x - capHalf, drawEnd.y},
                RenderPoint{drawEnd.x + capHalf, drawEnd.y},
                RenderStroke::Solid(color, strokeWidth));
        } else {
            renderer_.Renderer().DrawSolidLine(
                RenderPoint{drawStart.x, drawStart.y - capHalf},
                RenderPoint{drawStart.x, drawStart.y + capHalf},
                RenderStroke::Solid(color, strokeWidth));
            renderer_.Renderer().DrawSolidLine(
                RenderPoint{drawEnd.x, drawEnd.y - capHalf},
                RenderPoint{drawEnd.x, drawEnd.y + capHalf},
                RenderStroke::Solid(color, strokeWidth));
        }

        if (emphasized || hovered || overlayState.hoverOnExposedDashboard) {
            renderer_.Renderer().FillSolidRect(handleRect, color);
        } else {
            renderer_.Renderer().DrawSolidRect(
                handleRect, RenderStroke::Solid(color, static_cast<float>(handleOutline)));
        }
    }
}

void DashboardLayoutEditOverlayRenderer::DrawDottedHighlightRect(
    const RenderRect& rect, RenderColorId color, bool active, bool outside) const {
    if (rect.IsEmpty()) {
        return;
    }
    const int padding = std::max(1, renderer_.ScaleLogical(1));
    const RenderRect outlineRect = outside
        ? rect.Inflate(padding, padding)
        : RenderRect{rect.left + padding, rect.top + padding, rect.right - padding, rect.bottom - padding};
    const RenderRect drawRect = outlineRect.IsEmpty() ? rect : outlineRect;
    const int strokeWidth =
        active ? (std::max)(2, renderer_.ScaleLogical(2)) : (std::max)(1, renderer_.ScaleLogical(1));
    const int dotLength = (std::max)(strokeWidth + 1, renderer_.ScaleLogical(active ? 6 : 5));
    const int gapLength = (std::max)(strokeWidth + 1, renderer_.ScaleLogical(active ? 5 : 4));

    const auto drawHorizontal = [&](int y, int left, int right) {
        for (int x = left; x < right; x += dotLength + gapLength) {
            const int segmentRight = (std::min)(x + dotLength, right);
            renderer_.Renderer().FillSolidRect(RenderRect{x, y, segmentRight, y + strokeWidth}, color);
        }
    };
    const auto drawVertical = [&](int x, int top, int bottom) {
        for (int y = top; y < bottom; y += dotLength + gapLength) {
            const int segmentBottom = (std::min)(y + dotLength, bottom);
            renderer_.Renderer().FillSolidRect(RenderRect{x, y, x + strokeWidth, segmentBottom}, color);
        }
    };

    drawHorizontal(drawRect.top, drawRect.left, drawRect.right);
    drawHorizontal((std::max)(drawRect.top, drawRect.bottom - strokeWidth), drawRect.left, drawRect.right);
    drawVertical(drawRect.left, drawRect.top, drawRect.bottom);
    drawVertical((std::max)(drawRect.left, drawRect.right - strokeWidth), drawRect.top, drawRect.bottom);
}

void DashboardLayoutEditOverlayRenderer::DrawDottedAffordanceRect(
    const DashboardOverlayState& overlayState,
    LayoutEditOverlayAffordanceLayer layer,
    const RenderRect& rect,
    const std::vector<LayoutEditOverlayOwner>& owners,
    LayoutEditOverlayAffordanceLayer artifactLayer,
    RenderColorId color,
    bool active,
    bool outside) const {
    if (!ShouldDrawAffordanceLayer(artifactLayer, layer)) {
        return;
    }
    DrawDottedHighlightRect(ApplyOverlayDragOffset(overlayState, rect, owners), color, active, outside);
}

void DashboardLayoutEditOverlayRenderer::DrawLayoutSimilarityIndicators(
    const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const {
    if (!overlayState.ShouldDrawLayoutEditAffordances()) {
        return;
    }
    const int threshold = renderer_.LayoutSimilarityThreshold();
    if (threshold <= 0) {
        return;
    }

    LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
    const char* axisLabel = "horizontal";
    const LayoutEditGuide* activeGuide = nullptr;
    bool allWidgetsAffected = false;
    std::vector<const WidgetLayout*> allWidgets;
    if (overlayState.similarityIndicatorMode == LayoutSimilarityIndicatorMode::AllHorizontal) {
        axis = LayoutGuideAxis::Vertical;
        axisLabel = "horizontal";
        allWidgets = renderer_.CollectSimilarityIndicatorWidgets(axis);
        allWidgetsAffected = true;
    } else if (overlayState.similarityIndicatorMode == LayoutSimilarityIndicatorMode::AllVertical) {
        axis = LayoutGuideAxis::Horizontal;
        axisLabel = "vertical";
        allWidgets = renderer_.CollectSimilarityIndicatorWidgets(axis);
        allWidgetsAffected = true;
    } else {
        if (!overlayState.activeLayoutEditGuide.has_value()) {
            return;
        }
        activeGuide = &*overlayState.activeLayoutEditGuide;
        axis = activeGuide->axis;
        axisLabel = axis == LayoutGuideAxis::Vertical ? "horizontal" : "vertical";
        allWidgets = renderer_.CollectSimilarityIndicatorWidgets(axis);
    }
    bool hasAffectedWidget = false;
    for (const WidgetLayout* widget : allWidgets) {
        if (allWidgetsAffected ||
            (activeGuide != nullptr && renderer_.IsWidgetAffectedByGuide(*widget, *activeGuide))) {
            hasAffectedWidget = true;
            break;
        }
    }
    if (!hasAffectedWidget) {
        return;
    }
    const auto isAffectedWidget = [&](const WidgetLayout* widget) {
        return allWidgetsAffected ||
            (activeGuide != nullptr && renderer_.IsWidgetAffectedByGuide(*widget, *activeGuide));
    };

    // Size: one flat list tracks visibility, exact type, and ordinal; three vectors measured larger.
    std::vector<VisibleWidgetEntry> visibleWidgets;
    visibleWidgets.reserve(allWidgets.size());
    auto addVisibleWidget = [&](const WidgetLayout* widget) -> std::vector<VisibleWidgetEntry>::iterator {
        auto found = FindVisibleWidgetEntry(visibleWidgets, widget);
        if (found == visibleWidgets.end()) {
            visibleWidgets.push_back(VisibleWidgetEntry{widget, {}, false, 0});
            found = visibleWidgets.end() - 1;
        }
        return found;
    };
    auto addExactType = [&](const WidgetLayout* widget, SimilarityTypeKey type) {
        auto found = addVisibleWidget(widget);
        if (!found->hasExactType) {
            found->type = type;
            found->hasExactType = true;
        }
    };
    for (const WidgetLayout* affected : allWidgets) {
        if (!isAffectedWidget(affected)) {
            continue;
        }
        const int affectedExtent = renderer_.WidgetExtentForAxis(*affected, axis);
        if (affectedExtent <= 0 || affected->widget == nullptr) {
            continue;
        }
        const SimilarityTypeKey typeKey{affected->widgetClass, affectedExtent};
        bool hasExactMatch = false;
        for (const WidgetLayout* candidate : allWidgets) {
            if (candidate == affected || candidate->widget == nullptr ||
                candidate->widgetClass != affected->widgetClass) {
                continue;
            }
            const int candidateExtent = renderer_.WidgetExtentForAxis(*candidate, axis);
            if (candidateExtent <= 0 || std::abs(candidateExtent - affectedExtent) > threshold) {
                continue;
            }
            addVisibleWidget(affected);
            addVisibleWidget(candidate);
            if (candidateExtent == affectedExtent) {
                hasExactMatch = true;
                addExactType(candidate, typeKey);
            }
        }
        if (hasExactMatch) {
            addExactType(affected, typeKey);
        }
    }
    if (visibleWidgets.empty()) {
        return;
    }

    int nextOrdinal = 1;
    for (const WidgetLayout* widget : allWidgets) {
        auto visible = FindVisibleWidgetEntry(visibleWidgets, widget);
        if (visible == visibleWidgets.end() || !visible->hasExactType) {
            continue;
        }
        const auto existingType = std::find_if(visibleWidgets.begin(), visibleWidgets.end(), [&](const auto& entry) {
            return entry.hasExactType && entry.exactTypeOrdinal > 0 && entry.type == visible->type;
        });
        if (existingType != visibleWidgets.end()) {
            visible->exactTypeOrdinal = existingType->exactTypeOrdinal;
        } else {
            visible->exactTypeOrdinal = nextOrdinal++;
            // Perf: interactive drag traces intentionally drop renderer details, so avoid formatting them every paint.
            if (!renderer_.interactiveDragTraceActive_) {
                renderer_.WriteTraceFmt(
                    RES_STR("layout_similarity_group axis=\"%s\" class=%d extent=%d ordinal=%d"),
                    axisLabel,
                    static_cast<int>(visible->type.widgetClass),
                    visible->type.extent,
                    visible->exactTypeOrdinal);
            }
        }
    }

    const RenderColorId color = RenderColorId::LayoutGuide;
    const int inset = std::max(2, renderer_.ScaleLogical(4));
    const int cap = std::max(3, renderer_.ScaleLogical(4));
    const int offset = std::max(4, renderer_.ScaleLogical(6));
    const int notchDepth = std::max(3, renderer_.ScaleLogical(4));
    const int notchSpacing = std::max(3, renderer_.ScaleLogical(4));

    for (const WidgetLayout* widget : allWidgets) {
        auto visible = FindVisibleWidgetEntry(visibleWidgets, widget);
        if (visible == visibleWidgets.end()) {
            continue;
        }
        if (!ShouldDrawAffordanceLayer(widget->overlayLayer, layer)) {
            continue;
        }
        const int exactTypeOrdinal = visible->exactTypeOrdinal;
        const RenderRect rect = ApplyOverlayDragOffset(overlayState, widget->rect, widget->overlayOwners);
        if (axis == LayoutGuideAxis::Vertical) {
            const int y = rect.top + offset;
            const int left = rect.left + inset;
            const int right = rect.right - inset;
            const RenderStroke stroke = RenderStroke::Solid(color);
            renderer_.Renderer().DrawSolidLine(RenderPoint{left, y}, RenderPoint{right, y}, stroke);
            renderer_.Renderer().DrawSolidLine(RenderPoint{left + cap, y - cap}, RenderPoint{left, y}, stroke);
            renderer_.Renderer().DrawSolidLine(RenderPoint{left, y}, RenderPoint{left + cap, y + cap + 1}, stroke);
            renderer_.Renderer().DrawSolidLine(RenderPoint{right - cap, y - cap}, RenderPoint{right, y}, stroke);
            renderer_.Renderer().DrawSolidLine(RenderPoint{right, y}, RenderPoint{right - cap, y + cap + 1}, stroke);
            if (exactTypeOrdinal > 0) {
                const int cx = left + std::max(0, (right - left) / 2);
                const int count = exactTypeOrdinal;
                const int totalWidth = (count - 1) * notchSpacing;
                int notchX = cx - (totalWidth / 2);
                for (int i = 0; i < count; ++i) {
                    renderer_.Renderer().DrawSolidLine(
                        RenderPoint{notchX, y - notchDepth}, RenderPoint{notchX, y + notchDepth + 1}, stroke);
                    notchX += notchSpacing;
                }
            }
        } else {
            const int x = rect.left + offset;
            const int top = rect.top + inset;
            const int bottom = rect.bottom - inset;
            const RenderStroke stroke = RenderStroke::Solid(color);
            renderer_.Renderer().DrawSolidLine(RenderPoint{x, top}, RenderPoint{x, bottom}, stroke);
            renderer_.Renderer().DrawSolidLine(RenderPoint{x - cap, top + cap}, RenderPoint{x, top}, stroke);
            renderer_.Renderer().DrawSolidLine(RenderPoint{x, top}, RenderPoint{x + cap + 1, top + cap}, stroke);
            renderer_.Renderer().DrawSolidLine(RenderPoint{x - cap, bottom - cap}, RenderPoint{x, bottom}, stroke);
            renderer_.Renderer().DrawSolidLine(RenderPoint{x, bottom}, RenderPoint{x + cap + 1, bottom - cap}, stroke);
            if (exactTypeOrdinal > 0) {
                const int cy = top + std::max(0, (bottom - top) / 2);
                const int count = exactTypeOrdinal;
                const int totalHeight = (count - 1) * notchSpacing;
                int notchY = cy - (totalHeight / 2);
                for (int i = 0; i < count; ++i) {
                    renderer_.Renderer().DrawSolidLine(
                        RenderPoint{x - notchDepth, notchY}, RenderPoint{x + notchDepth + 1, notchY}, stroke);
                    notchY += notchSpacing;
                }
            }
        }
    }
}

bool DashboardLayoutEditOverlayRenderer::HasForegroundAffordanceLayer(const DashboardOverlayState& overlayState) const {
    return overlayState.activeMetricListReorderDrag.has_value() ||
        overlayState.activeContainerChildReorderDrag.has_value();
}

bool DashboardLayoutEditOverlayRenderer::ShouldDrawAffordanceLayer(
    LayoutEditOverlayAffordanceLayer artifactLayer, LayoutEditOverlayAffordanceLayer drawLayer) const {
    return artifactLayer == drawLayer;
}

std::optional<RenderPoint> DashboardLayoutEditOverlayRenderer::OverlayDragOffsetForOwners(
    const DashboardOverlayState& overlayState, const std::vector<LayoutEditOverlayOwner>& owners) const {
    if (!overlayState.activeContainerChildReorderDrag.has_value()) {
        return std::nullopt;
    }
    const ContainerChildReorderOverlayState& drag = *overlayState.activeContainerChildReorderDrag;
    const auto matchesDrag = [&](const LayoutEditOverlayOwner& owner) {
        return owner.childIndex == drag.currentIndex &&
            MatchesLayoutContainerEditKey(
                   LayoutContainerEditKey{owner.key.editCardId, owner.key.nodePath},
                   LayoutContainerEditKey{drag.key.editCardId, drag.key.nodePath});
    };
    if (std::none_of(owners.begin(), owners.end(), matchesDrag)) {
        return std::nullopt;
    }
    if (drag.currentIndex < 0 || drag.currentIndex >= static_cast<int>(drag.childRects.size())) {
        return std::nullopt;
    }
    const RenderRect& childRect = drag.childRects[static_cast<size_t>(drag.currentIndex)];
    const int childStart = drag.horizontal ? childRect.left : childRect.top;
    const int offset = drag.mouseCoordinate - drag.dragOffset - childStart;
    return drag.horizontal ? RenderPoint{offset, 0} : RenderPoint{0, offset};
}

std::optional<RenderPoint> DashboardLayoutEditOverlayRenderer::OverlayDragOffsetForAnchor(
    const DashboardOverlayState& overlayState, const LayoutEditAnchorRegion& region) const {
    if (overlayState.activeMetricListReorderDrag.has_value()) {
        const std::optional<LayoutNodeFieldEditKey> nodeFieldKey = LayoutEditAnchorNodeFieldKey(region.key);
        const MetricListReorderOverlayState& drag = *overlayState.activeMetricListReorderDrag;
        if (nodeFieldKey.has_value() && nodeFieldKey->widgetClass == WidgetClass::MetricList &&
            region.key.anchorId == drag.currentIndex && ::MatchesWidgetIdentity(drag.widget, region.key.widget)) {
            return RenderPoint{0, drag.mouseY - drag.dragOffsetY - region.targetRect.top};
        }
    }
    return OverlayDragOffsetForOwners(overlayState, region.overlayOwners);
}

RenderRect DashboardLayoutEditOverlayRenderer::ApplyOverlayDragOffset(
    const DashboardOverlayState& overlayState,
    const RenderRect& rect,
    const std::vector<LayoutEditOverlayOwner>& owners) const {
    if (const auto offset = OverlayDragOffsetForOwners(overlayState, owners); offset.has_value()) {
        return OffsetRect(rect, offset->x, offset->y);
    }
    return rect;
}

RenderPoint DashboardLayoutEditOverlayRenderer::ApplyOverlayDragOffset(
    const DashboardOverlayState& overlayState,
    RenderPoint point,
    const std::vector<LayoutEditOverlayOwner>& owners) const {
    if (const auto offset = OverlayDragOffsetForOwners(overlayState, owners); offset.has_value()) {
        point.x += offset->x;
        point.y += offset->y;
    }
    return point;
}

void DashboardLayoutEditOverlayRenderer::ApplyOverlayDragOffset(
    const DashboardOverlayState& overlayState, LayoutEditAnchorRegion& region) const {
    if (const auto offset = OverlayDragOffsetForAnchor(overlayState, region); offset.has_value()) {
        region.targetRect = OffsetRect(region.targetRect, offset->x, offset->y);
        region.anchorRect = OffsetRect(region.anchorRect, offset->x, offset->y);
        region.anchorHitRect = OffsetRect(region.anchorHitRect, offset->x, offset->y);
    }
}

bool DashboardLayoutEditOverlayRenderer::ShouldSkipBaseWidget(
    const DashboardOverlayState& overlayState, const WidgetLayout& widget) const {
    return OverlayDragOffsetForOwners(overlayState, widget.overlayOwners).has_value();
}

bool DashboardLayoutEditOverlayRenderer::ShouldSkipForContainerChildReorder(const WidgetLayout& widget) const {
    return renderer_.activeOverlayState_ != nullptr &&
        OverlayDragOffsetForOwners(*renderer_.activeOverlayState_, widget.overlayOwners).has_value();
}

void DashboardLayoutEditOverlayRenderer::DrawContainerChildReorderOverlay(const MetricSource& metrics) {
    if (renderer_.activeOverlayState_ == nullptr ||
        !renderer_.activeOverlayState_->activeContainerChildReorderDrag.has_value()) {
        return;
    }

    const ContainerChildReorderOverlayState& drag = *renderer_.activeOverlayState_->activeContainerChildReorderDrag;
    if (drag.currentIndex < 0 || drag.currentIndex >= static_cast<int>(drag.childRects.size())) {
        return;
    }

    const RenderRect& childRect = drag.childRects[static_cast<size_t>(drag.currentIndex)];
    const int childStart = drag.horizontal ? childRect.left : childRect.top;
    const int offset = drag.mouseCoordinate - drag.dragOffset - childStart;

    renderer_.PushWidgetAnimationTranslation(drag.horizontal ? RenderPoint{offset, 0} : RenderPoint{0, offset});
    for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
        if (ShouldSkipForContainerChildReorder(card.chrome)) {
            renderer_.DrawResolvedWidget(card.chrome, metrics);
        }
        for (const auto& widget : card.widgets) {
            if (ShouldSkipForContainerChildReorder(widget)) {
                renderer_.DrawResolvedWidget(widget, metrics);
            }
        }
    }
    renderer_.Renderer().DrawSolidRect(
        childRect,
        RenderStroke::Dotted(
            ActiveEditColor(*renderer_.activeOverlayState_),
            static_cast<float>(
                UseActiveEditEmphasis(*renderer_.activeOverlayState_, true)
                    ? (std::max)(2, renderer_.ScaleLogical(2))
                    : (std::max)(1, renderer_.ScaleLogical(1)))));
    renderer_.PopWidgetAnimationTranslation();
}
