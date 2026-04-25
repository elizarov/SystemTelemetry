#include "dashboard_renderer/impl/layout_edit_overlay_renderer.h"

#include <algorithm>
#include <cmath>
#include <d2d1.h>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dashboard_renderer/dashboard_renderer.h"
#include "dashboard_renderer/impl/layout_resolver.h"
#include "layout_edit/layout_edit_helpers.h"
#include "layout_edit/layout_edit_parameter_metadata.h"

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
    return RenderRect{(std::min)(left.left, right.left),
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

}  // namespace

DashboardLayoutEditOverlayRenderer::DashboardLayoutEditOverlayRenderer(
    DashboardRenderer& renderer, DashboardLayoutResolver& layoutResolver)
    : renderer_(renderer), layoutResolver_(layoutResolver) {}

void DashboardLayoutEditOverlayRenderer::Draw(const DashboardOverlayState& overlayState, const MetricSource& metrics) {
    DrawContainerChildReorderOverlay(metrics);
    DrawSelectedColorEditHighlights(overlayState);
    DrawSelectedTreeNodeHighlight(overlayState);
    DrawHoveredWidgetHighlight(overlayState);
    DrawHoveredEditableAnchorHighlight(overlayState);
    DrawLayoutEditGuides(overlayState);
    DrawGapEditAnchors(overlayState);
    DrawWidgetEditGuides(overlayState);
    DrawLayoutSimilarityIndicators(overlayState);
}

void DashboardLayoutEditOverlayRenderer::DrawHoveredWidgetHighlight(const DashboardOverlayState& overlayState) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawLayoutEditAffordances()) {
        return;
    }
    const std::optional<RenderRect> hoveredRect = FindHoveredWidgetOutlineRect(overlayState);
    if (!hoveredRect.has_value() || hoveredRect->IsEmpty()) {
        return;
    }
    renderer_.DrawSolidRect(*hoveredRect, RenderStroke::Solid(RenderColorId::LayoutGuide));
}

void DashboardLayoutEditOverlayRenderer::DrawHoveredEditableAnchorHighlight(
    const DashboardOverlayState& overlayState) const {
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
        const auto collect = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (!::MatchesWidgetIdentity(region.key.widget, source.key.widget) ||
                    !SameRect(region.targetRect, source.targetRect)) {
                    continue;
                }
                LayoutEditAnchorRegion highlightedRegion = region;
                if (!MatchesEditableAnchorKey(region.key, source.key)) {
                    highlightedRegion.drawTargetOutline = false;
                }
                appendHighlight(highlightedRegion, active && MatchesEditableAnchorKey(region.key, source.key));
            }
        };
        collect(layoutResolver_.staticEditableAnchorRegions_);
        collect(layoutResolver_.dynamicEditableAnchorRegions_);
    };
    const auto appendByKey = [&](const std::optional<LayoutEditAnchorKey>& key, bool active) {
        if (!key.has_value()) {
            return;
        }
        const auto region = renderer_.FindEditableAnchorRegion(*key);
        if (region.has_value()) {
            appendRelatedHighlights(*region, active);
        }
    };
    if (overlayState.selectedTreeHighlight.has_value()) {
        const auto collectSelected = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
            for (const auto& region : regions) {
                if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, region.key) ||
                    (std::holds_alternative<LayoutEditSelectionHighlightSpecial>(*overlayState.selectedTreeHighlight) &&
                        std::get<LayoutEditSelectionHighlightSpecial>(*overlayState.selectedTreeHighlight) ==
                            LayoutEditSelectionHighlightSpecial::AllTexts &&
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
    appendByKey(overlayState.hoveredEditableAnchor, false);
    appendByKey(overlayState.activeEditableAnchor, true);
    if (highlights.empty()) {
        return;
    }
    const auto moveActiveReorderHighlight = [&](LayoutEditAnchorRegion& region) {
        bool movedByActiveAnchor = false;
        if (const auto orderKey = LayoutEditAnchorMetricListOrderKey(region.key);
            orderKey.has_value() && overlayState.activeMetricListReorderDrag.has_value()) {
            const MetricListReorderOverlayState& drag = *overlayState.activeMetricListReorderDrag;
            if (drag.currentIndex == region.key.anchorId && ::MatchesWidgetIdentity(drag.widget, region.key.widget)) {
                const int dy = drag.mouseY - drag.dragOffsetY - region.targetRect.top;
                region.targetRect = OffsetRect(region.targetRect, 0, dy);
                region.anchorRect = OffsetRect(region.anchorRect, 0, dy);
                region.anchorHitRect = OffsetRect(region.anchorHitRect, 0, dy);
                movedByActiveAnchor = true;
            }
            return;
        }
        if (const auto orderKey = LayoutEditAnchorContainerChildOrderKey(region.key);
            orderKey.has_value() && overlayState.activeContainerChildReorderDrag.has_value()) {
            const ContainerChildReorderOverlayState& drag = *overlayState.activeContainerChildReorderDrag;
            if (drag.currentIndex == region.key.anchorId &&
                MatchesLayoutContainerEditKey(LayoutContainerEditKey{drag.key.editCardId, drag.key.nodePath},
                    LayoutContainerEditKey{orderKey->editCardId, orderKey->nodePath})) {
                const int childStart = drag.horizontal ? region.targetRect.left : region.targetRect.top;
                const int offset = drag.mouseCoordinate - drag.dragOffset - childStart;
                const int dx = drag.horizontal ? offset : 0;
                const int dy = drag.horizontal ? 0 : offset;
                region.targetRect = OffsetRect(region.targetRect, dx, dy);
                region.anchorRect = OffsetRect(region.anchorRect, dx, dy);
                region.anchorHitRect = OffsetRect(region.anchorHitRect, dx, dy);
                movedByActiveAnchor = true;
            }
        }
        if (!movedByActiveAnchor) {
            region.targetRect = ApplyContainerChildReorderOffset(region.targetRect);
            region.anchorRect = ApplyContainerChildReorderOffset(region.anchorRect);
            region.anchorHitRect = ApplyContainerChildReorderOffset(region.anchorHitRect);
        }
    };
    const std::optional<RenderRect> hoveredWidgetOutlineRect = FindHoveredWidgetOutlineRect(overlayState);
    for (const auto& highlight : highlights) {
        LayoutEditAnchorRegion highlighted = highlight.first;
        const bool active = highlight.second;
        if (active || overlayState.activeContainerChildReorderDrag.has_value()) {
            moveActiveReorderHighlight(highlighted);
        }
        const RenderColorId outlineColor = active ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        bool drawTargetOutline = highlighted.drawTargetOutline && !highlighted.targetRect.IsEmpty();
        if (!active && drawTargetOutline && hoveredWidgetOutlineRect.has_value() &&
            SameRect(highlighted.targetRect, *hoveredWidgetOutlineRect)) {
            drawTargetOutline = false;
        }
        if (drawTargetOutline) {
            DrawDottedHighlightRect(highlighted.targetRect, outlineColor, active);
        }

        if (highlighted.shape == AnchorShape::Circle) {
            const float outlineWidth = static_cast<float>(
                active ? (std::max)(2, renderer_.ScaleLogical(2)) : (std::max)(1, renderer_.ScaleLogical(1)));
            renderer_.DrawSolidEllipse(highlighted.anchorRect, RenderStroke::Solid(outlineColor, outlineWidth));
        } else if (highlighted.shape == AnchorShape::Diamond) {
            renderer_.FillSolidDiamond(highlighted.anchorRect, outlineColor);
        } else if (highlighted.shape == AnchorShape::Wedge) {
            const float outlineWidth = static_cast<float>(
                active ? (std::max)(2, renderer_.ScaleLogical(2)) : (std::max)(1, renderer_.ScaleLogical(1)));
            const RenderPoint topRight{highlighted.anchorRect.right, highlighted.anchorRect.top};
            const RenderPoint bottomLeft{highlighted.anchorRect.left, highlighted.anchorRect.bottom};
            const RenderPoint bottomRight{highlighted.anchorRect.right, highlighted.anchorRect.bottom};
            renderer_.DrawSolidLine(bottomLeft, bottomRight, RenderStroke::Solid(outlineColor, outlineWidth));
            renderer_.DrawSolidLine(topRight, bottomRight, RenderStroke::Solid(outlineColor, outlineWidth));
        } else if (highlighted.shape == AnchorShape::VerticalReorder ||
                   highlighted.shape == AnchorShape::HorizontalReorder) {
            const float outlineWidth = static_cast<float>(
                active ? (std::max)(2, renderer_.ScaleLogical(2)) : (std::max)(1, renderer_.ScaleLogical(1)));
            const int centerX = highlighted.anchorRect.left +
                                (std::max<LONG>(0, highlighted.anchorRect.right - highlighted.anchorRect.left) / 2);
            const int centerY = highlighted.anchorRect.top +
                                (std::max<LONG>(0, highlighted.anchorRect.bottom - highlighted.anchorRect.top) / 2);
            const int gapHalf = (std::max)(1, renderer_.ScaleLogical(1));
            const auto stroke = RenderStroke::Solid(outlineColor, outlineWidth);
            if (highlighted.shape == AnchorShape::HorizontalReorder) {
                const int halfHeight =
                    (std::max)(1, static_cast<int>(highlighted.anchorRect.bottom - highlighted.anchorRect.top) / 2);
                const RenderPoint leftApex{highlighted.anchorRect.left, centerY};
                const RenderPoint leftTop{centerX - gapHalf, centerY - halfHeight};
                const RenderPoint leftBottom{centerX - gapHalf, centerY + halfHeight};
                const RenderPoint rightApex{highlighted.anchorRect.right, centerY};
                const RenderPoint rightTop{centerX + gapHalf, centerY - halfHeight};
                const RenderPoint rightBottom{centerX + gapHalf, centerY + halfHeight};
                renderer_.DrawSolidLine(leftApex, leftTop, stroke);
                renderer_.DrawSolidLine(leftTop, leftBottom, stroke);
                renderer_.DrawSolidLine(leftBottom, leftApex, stroke);
                renderer_.DrawSolidLine(rightTop, rightApex, stroke);
                renderer_.DrawSolidLine(rightApex, rightBottom, stroke);
                renderer_.DrawSolidLine(rightBottom, rightTop, stroke);
            } else {
                const int halfWidth =
                    (std::max)(1, static_cast<int>(highlighted.anchorRect.right - highlighted.anchorRect.left) / 2);
                const RenderPoint upApex{centerX, highlighted.anchorRect.top};
                const RenderPoint upLeft{centerX - halfWidth, centerY - gapHalf};
                const RenderPoint upRight{centerX + halfWidth, centerY - gapHalf};
                const RenderPoint downApex{centerX, highlighted.anchorRect.bottom};
                const RenderPoint downLeft{centerX - halfWidth, centerY + gapHalf};
                const RenderPoint downRight{centerX + halfWidth, centerY + gapHalf};
                renderer_.DrawSolidLine(upApex, upLeft, stroke);
                renderer_.DrawSolidLine(upLeft, upRight, stroke);
                renderer_.DrawSolidLine(upRight, upApex, stroke);
                renderer_.DrawSolidLine(downLeft, downApex, stroke);
                renderer_.DrawSolidLine(downApex, downRight, stroke);
                renderer_.DrawSolidLine(downRight, downLeft, stroke);
            }
        } else if (highlighted.shape == AnchorShape::Plus) {
            const float outlineWidth = static_cast<float>(
                active ? (std::max)(2, renderer_.ScaleLogical(2)) : (std::max)(1, renderer_.ScaleLogical(1)));
            const int centerX = highlighted.anchorRect.left +
                                (std::max<LONG>(0, highlighted.anchorRect.right - highlighted.anchorRect.left) / 2);
            const int centerY = highlighted.anchorRect.top +
                                (std::max<LONG>(0, highlighted.anchorRect.bottom - highlighted.anchorRect.top) / 2);
            const int halfWidth =
                (std::max)(2, static_cast<int>(highlighted.anchorRect.right - highlighted.anchorRect.left) / 2);
            const int halfHeight =
                (std::max)(2, static_cast<int>(highlighted.anchorRect.bottom - highlighted.anchorRect.top) / 2);
            const auto stroke = RenderStroke::Solid(outlineColor, outlineWidth);
            renderer_.DrawSolidLine(
                RenderPoint{centerX - halfWidth, centerY}, RenderPoint{centerX + halfWidth, centerY}, stroke);
            renderer_.DrawSolidLine(
                RenderPoint{centerX, centerY - halfHeight}, RenderPoint{centerX, centerY + halfHeight}, stroke);
        } else {
            renderer_.FillSolidRect(highlighted.anchorRect, outlineColor);
        }
    }
}

std::optional<RenderRect> DashboardLayoutEditOverlayRenderer::FindHoveredWidgetOutlineRect(
    const DashboardOverlayState& overlayState) const {
    if (overlayState.hoveredEditableWidget.has_value() &&
        overlayState.hoveredEditableWidget->kind == LayoutEditWidgetIdentity::Kind::Widget) {
        for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
            for (const auto& widget : card.widgets) {
                if (renderer_.MatchesWidgetIdentity(widget, *overlayState.hoveredEditableWidget)) {
                    return ApplyContainerChildReorderOffset(widget.rect);
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
                return ApplyContainerChildReorderOffset(titleRect);
            }
        }
    }

    return std::nullopt;
}

void DashboardLayoutEditOverlayRenderer::DrawSelectedColorEditHighlights(
    const DashboardOverlayState& overlayState) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawSelectedTreeHighlight()) {
        return;
    }

    std::vector<RenderRect> highlightedRects;
    const auto appendRect = [&](const RenderRect& rect) {
        if (rect.IsEmpty()) {
            return;
        }
        const RenderRect adjustedRect = ApplyContainerChildReorderOffset(rect);
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
                appendRect(region.targetRect);
            }
        }
    };
    collect(layoutResolver_.staticColorEditRegions_);
    collect(layoutResolver_.dynamicColorEditRegions_);
    for (const auto& rect : highlightedRects) {
        DrawDottedHighlightRect(rect, RenderColorId::ActiveEdit, true);
    }
}

void DashboardLayoutEditOverlayRenderer::DrawSelectedTreeNodeHighlight(
    const DashboardOverlayState& overlayState) const {
    if (overlayState.IsContainerGuideDragActive()) {
        return;
    }
    if (!overlayState.ShouldDrawSelectedTreeHighlight()) {
        return;
    }

    const RenderColorId color = RenderColorId::ActiveEdit;
    std::vector<RenderRect> selectedRects;
    bool drawDashboardBoundsOutline = false;
    const auto appendRect = [&](const RenderRect& rect) {
        if (rect.IsEmpty()) {
            return;
        }
        const RenderRect adjustedRect = ApplyContainerChildReorderOffset(rect);
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
                    appendRect(widget.rect);
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
                    appendRect(card.rect);
                }
            }
        }
    }
    for (const auto& guide : layoutResolver_.layoutEditGuides_) {
        const bool matchesFocus = MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide);
        const bool matchesContainer =
            std::holds_alternative<LayoutContainerEditKey>(*overlayState.selectedTreeHighlight) &&
            MatchesLayoutContainerEditKey(std::get<LayoutContainerEditKey>(*overlayState.selectedTreeHighlight),
                {guide.editCardId, guide.nodePath});
        if (matchesFocus || matchesContainer) {
            appendRect(guide.containerRect);
        }
    }
    for (const auto& guide : layoutResolver_.widgetEditGuides_) {
        if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide)) {
            appendRect(guide.widgetRect);
        }
    }
    const auto collectAnchorTargets = [&](const std::vector<LayoutEditAnchorRegion>& regions) {
        for (const auto& region : regions) {
            if (MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, region.key) ||
                (std::holds_alternative<LayoutEditSelectionHighlightSpecial>(*overlayState.selectedTreeHighlight) &&
                    std::get<LayoutEditSelectionHighlightSpecial>(*overlayState.selectedTreeHighlight) ==
                        LayoutEditSelectionHighlightSpecial::AllTexts &&
                    LayoutEditAnchorParameter(region.key).has_value() &&
                    IsFontEditParameter(*LayoutEditAnchorParameter(region.key)))) {
                if (const auto parameter = LayoutEditAnchorParameter(region.key); parameter.has_value()) {
                    if (!SuppressAnchorTargetSelectionOutline(*parameter)) {
                        appendRect(region.targetRect);
                    }
                    if (UseWholeWidgetSelectionOutline(*parameter)) {
                        appendWidgetRectsForIdentity(region.key.widget);
                    }
                } else {
                    appendRect(region.targetRect);
                }
            }
        }
    };
    collectAnchorTargets(layoutResolver_.staticEditableAnchorRegions_);
    collectAnchorTargets(layoutResolver_.dynamicEditableAnchorRegions_);
    for (const auto& rect : selectedRects) {
        DrawDottedHighlightRect(rect, color, true);
    }
    if (drawDashboardBoundsOutline) {
        DrawDottedHighlightRect(
            RenderRect{0, 0, layoutResolver_.resolvedLayout_.windowWidth, layoutResolver_.resolvedLayout_.windowHeight},
            color,
            true,
            false);
    }

    if (const auto* widgetClass = std::get_if<WidgetClass>(&*overlayState.selectedTreeHighlight)) {
        for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
            for (const auto& widget : card.widgets) {
                if (widget.widget != nullptr && widget.widget->Class() == *widgetClass) {
                    DrawDottedHighlightRect(widget.rect, color, true);
                }
            }
        }
        return;
    }

    if (const auto* widgetIdentity = std::get_if<LayoutEditWidgetIdentity>(&*overlayState.selectedTreeHighlight)) {
        if (widgetIdentity->kind == LayoutEditWidgetIdentity::Kind::CardChrome) {
            std::vector<RenderRect> embeddedInstanceRects;
            const auto appendEmbeddedRect = [&](const RenderRect& rect) {
                if (rect.IsEmpty()) {
                    return;
                }
                const auto existing = std::find_if(
                    embeddedInstanceRects.begin(), embeddedInstanceRects.end(), [&](const RenderRect& candidate) {
                        return candidate.left == rect.left && candidate.top == rect.top &&
                               candidate.right == rect.right && candidate.bottom == rect.bottom;
                    });
                if (existing == embeddedInstanceRects.end()) {
                    embeddedInstanceRects.push_back(rect);
                }
            };
            for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
                const LayoutEditWidgetIdentity cardIdentity{
                    card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
                if (MatchesCardChromeSelectionIdentity(*widgetIdentity, cardIdentity)) {
                    DrawDottedHighlightRect(card.rect, color, true);
                }
            }
            for (const auto& guide : layoutResolver_.layoutEditGuides_) {
                const LayoutEditWidgetIdentity cardIdentity{
                    guide.renderCardId, guide.editCardId, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
                if (guide.renderCardId.empty() || guide.renderCardId == guide.editCardId || !guide.nodePath.empty() ||
                    !MatchesCardChromeSelectionIdentity(*widgetIdentity, cardIdentity)) {
                    continue;
                }
                appendEmbeddedRect(guide.containerRect);
            }
            if (embeddedInstanceRects.empty()) {
                for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
                    RenderRect embeddedBounds{};
                    for (const auto& widget : card.widgets) {
                        const LayoutEditWidgetIdentity cardIdentity{
                            widget.cardId, widget.editCardId, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
                        if (widget.cardId == widget.editCardId ||
                            !MatchesCardChromeSelectionIdentity(*widgetIdentity, cardIdentity)) {
                            continue;
                        }
                        embeddedBounds = UnionRect(embeddedBounds, widget.rect);
                    }
                    appendEmbeddedRect(embeddedBounds);
                }
            }
            for (const auto& rect : embeddedInstanceRects) {
                DrawDottedHighlightRect(rect, color, true);
            }
            return;
        }
    }

    if (const auto* special = std::get_if<LayoutEditSelectionHighlightSpecial>(&*overlayState.selectedTreeHighlight)) {
        if (*special == LayoutEditSelectionHighlightSpecial::AllCards) {
            for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
                DrawDottedHighlightRect(card.rect, color, true);
            }
            return;
        }
        if (*special == LayoutEditSelectionHighlightSpecial::AllTexts) {
            for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
                for (const auto& widget : card.widgets) {
                    if (widget.widget != nullptr && widget.widget->Class() == WidgetClass::Text) {
                        DrawDottedHighlightRect(widget.rect, color, true);
                    }
                }
            }
        }
        if (*special == LayoutEditSelectionHighlightSpecial::DashboardBounds) {
            DrawDottedHighlightRect(
                RenderRect{
                    0, 0, layoutResolver_.resolvedLayout_.windowWidth, layoutResolver_.resolvedLayout_.windowHeight},
                color,
                true,
                false);
            return;
        }
    }
}

void DashboardLayoutEditOverlayRenderer::DrawLayoutEditGuides(const DashboardOverlayState& overlayState) const {
    if (!overlayState.ShouldDrawLayoutEditAffordances() || layoutResolver_.layoutEditGuides_.empty()) {
        return;
    }

    std::vector<std::pair<RenderRect, bool>> containerHighlights;
    const auto appendContainerHighlight = [&](const RenderRect& rect, bool active) {
        if (rect.IsEmpty()) {
            return;
        }
        const RenderRect adjustedRect = ApplyContainerChildReorderOffset(rect);
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
    if (overlayState.hoveredLayoutEditGuide.has_value()) {
        appendContainerHighlight(overlayState.hoveredLayoutEditGuide->containerRect, false);
    }
    if (overlayState.activeLayoutEditGuide.has_value()) {
        appendContainerHighlight(overlayState.activeLayoutEditGuide->containerRect, true);
    }
    for (const auto& [rect, active] : containerHighlights) {
        const RenderColorId color = active ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        DrawDottedHighlightRect(rect, color, active);
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
        const RenderColorId color = emphasized ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        const RenderPoint start =
            ApplyContainerChildReorderOffset(RenderPoint{guide.lineRect.left, guide.lineRect.top}, guide.lineRect);
        const RenderPoint end = ApplyContainerChildReorderOffset(
            guide.axis == LayoutGuideAxis::Vertical ? RenderPoint{guide.lineRect.left, guide.lineRect.bottom}
                                                    : RenderPoint{guide.lineRect.right, guide.lineRect.top},
            guide.lineRect);
        renderer_.DrawSolidLine(
            start, end, RenderStroke::Solid(color, static_cast<float>(emphasized ? activeLineWidth : lineWidth)));
    }
}

void DashboardLayoutEditOverlayRenderer::DrawWidgetEditGuides(const DashboardOverlayState& overlayState) const {
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
        const bool active = overlayState.activeWidgetEditGuide.has_value() &&
                            MatchesWidgetEditGuide(guide, *overlayState.activeWidgetEditGuide);
        const bool selected = !overlayState.activeWidgetEditGuide.has_value() &&
                              overlayState.selectedTreeHighlight.has_value() &&
                              MatchesLayoutEditSelectionHighlight(*overlayState.selectedTreeHighlight, guide);
        const bool emphasized = active || selected;
        const RenderColorId color = emphasized ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        renderer_.DrawSolidLine(ApplyContainerChildReorderOffset(guide.drawStart, guide.widgetRect),
            ApplyContainerChildReorderOffset(guide.drawEnd, guide.widgetRect),
            RenderStroke::Solid(color, static_cast<float>(lineWidth)));
    }
}

void DashboardLayoutEditOverlayRenderer::DrawGapEditAnchors(const DashboardOverlayState& overlayState) const {
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
        if (overlayState.selectedTreeHighlight.has_value() && !overlayState.hoverOnExposedDashboard &&
            !overlayState.hoveredLayoutCard.has_value() && !overlayState.hoveredGapEditAnchor.has_value()) {
            return false;
        }
        if (overlayState.hoverOnExposedDashboard) {
            return true;
        }
        if (anchor.key.widget.kind == LayoutEditWidgetIdentity::Kind::DashboardChrome) {
            return true;
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
        const RenderColorId color = emphasized ? RenderColorId::ActiveEdit : RenderColorId::LayoutGuide;
        const float strokeWidth = static_cast<float>(lineWidth);
        const RenderRect offsetSource = anchor.hitRect.IsEmpty() ? anchor.handleRect : anchor.hitRect;
        const RenderPoint drawStart = ApplyContainerChildReorderOffset(anchor.drawStart, offsetSource);
        const RenderPoint drawEnd = ApplyContainerChildReorderOffset(anchor.drawEnd, offsetSource);
        const RenderRect handleRect = ApplyContainerChildReorderOffset(anchor.handleRect);

        renderer_.DrawSolidLine(drawStart, drawEnd, RenderStroke::Solid(color, strokeWidth));
        if (anchor.axis == LayoutGuideAxis::Vertical) {
            renderer_.DrawSolidLine(RenderPoint{drawStart.x - capHalf, drawStart.y},
                RenderPoint{drawStart.x + capHalf, drawStart.y},
                RenderStroke::Solid(color, strokeWidth));
            renderer_.DrawSolidLine(RenderPoint{drawEnd.x - capHalf, drawEnd.y},
                RenderPoint{drawEnd.x + capHalf, drawEnd.y},
                RenderStroke::Solid(color, strokeWidth));
        } else {
            renderer_.DrawSolidLine(RenderPoint{drawStart.x, drawStart.y - capHalf},
                RenderPoint{drawStart.x, drawStart.y + capHalf},
                RenderStroke::Solid(color, strokeWidth));
            renderer_.DrawSolidLine(RenderPoint{drawEnd.x, drawEnd.y - capHalf},
                RenderPoint{drawEnd.x, drawEnd.y + capHalf},
                RenderStroke::Solid(color, strokeWidth));
        }

        if (emphasized || hovered || overlayState.hoverOnExposedDashboard) {
            renderer_.FillSolidRect(handleRect, color);
        } else {
            renderer_.DrawSolidRect(handleRect, RenderStroke::Solid(color, static_cast<float>(handleOutline)));
        }
    }
}

void DashboardLayoutEditOverlayRenderer::DrawDottedHighlightRect(
    const RenderRect& rect, RenderColorId color, bool active, bool outside) const {
    if (rect.IsEmpty()) {
        return;
    }
    const int padding = std::max(1, renderer_.ScaleLogical(1));
    const RenderRect outlineRect =
        outside ? rect.Inflate(padding, padding)
                : RenderRect{rect.left + padding, rect.top + padding, rect.right - padding, rect.bottom - padding};
    const float outlineWidth = static_cast<float>(
        active ? (std::max)(2, renderer_.ScaleLogical(2)) : (std::max)(1, renderer_.ScaleLogical(1)));
    const RenderRect drawRect = outlineRect.IsEmpty() ? rect : outlineRect;
    auto& renderer = renderer_;
    const int strokeWidth = (std::max)(1, static_cast<int>(std::lround(outlineWidth)));
    const int dotLength = (std::max)(strokeWidth + 1, renderer_.ScaleLogical(active ? 6 : 5));
    const int gapLength = (std::max)(strokeWidth + 1, renderer_.ScaleLogical(active ? 5 : 4));

    const auto drawHorizontal = [&](int y, int left, int right) {
        for (int x = left; x < right; x += dotLength + gapLength) {
            const int segmentRight = (std::min)(x + dotLength, right);
            renderer.FillSolidRect(RenderRect{x, y, segmentRight, y + strokeWidth}, color);
        }
    };
    const auto drawVertical = [&](int x, int top, int bottom) {
        for (int y = top; y < bottom; y += dotLength + gapLength) {
            const int segmentBottom = (std::min)(y + dotLength, bottom);
            renderer.FillSolidRect(RenderRect{x, y, x + strokeWidth, segmentBottom}, color);
        }
    };

    drawHorizontal(drawRect.top, drawRect.left, drawRect.right);
    drawHorizontal((std::max)(drawRect.top, drawRect.bottom - strokeWidth), drawRect.left, drawRect.right);
    drawVertical(drawRect.left, drawRect.top, drawRect.bottom);
    drawVertical((std::max)(drawRect.left, drawRect.right - strokeWidth), drawRect.top, drawRect.bottom);
}

void DashboardLayoutEditOverlayRenderer::DrawLayoutSimilarityIndicators(
    const DashboardOverlayState& overlayState) const {
    if (!overlayState.ShouldDrawLayoutEditAffordances()) {
        return;
    }
    const int threshold = renderer_.LayoutSimilarityThreshold();
    if (threshold <= 0) {
        return;
    }

    struct SimilarityTypeKey {
        WidgetClass widgetClass = WidgetClass::Unknown;
        int extent = 0;

        bool operator==(const SimilarityTypeKey& other) const {
            return widgetClass == other.widgetClass && extent == other.extent;
        }

        bool operator<(const SimilarityTypeKey& other) const {
            if (widgetClass != other.widgetClass) {
                return widgetClass < other.widgetClass;
            }
            return extent < other.extent;
        }
    };

    struct SimilarityTypeKeyHash {
        size_t operator()(const SimilarityTypeKey& key) const {
            size_t hash = std::hash<int>{}(static_cast<int>(key.widgetClass));
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.extent);
            return hash;
        }
    };

    LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
    const char* axisLabel = "horizontal";
    std::vector<const WidgetLayout*> affectedWidgets;
    std::vector<const WidgetLayout*> allWidgets;
    if (overlayState.similarityIndicatorMode == LayoutSimilarityIndicatorMode::AllHorizontal) {
        axis = LayoutGuideAxis::Vertical;
        axisLabel = "horizontal";
        allWidgets = renderer_.CollectSimilarityIndicatorWidgets(axis);
        affectedWidgets = allWidgets;
    } else if (overlayState.similarityIndicatorMode == LayoutSimilarityIndicatorMode::AllVertical) {
        axis = LayoutGuideAxis::Horizontal;
        axisLabel = "vertical";
        allWidgets = renderer_.CollectSimilarityIndicatorWidgets(axis);
        affectedWidgets = allWidgets;
    } else {
        if (!overlayState.activeLayoutEditGuide.has_value()) {
            return;
        }
        const LayoutEditGuide& guide = *overlayState.activeLayoutEditGuide;
        axis = guide.axis;
        axisLabel = axis == LayoutGuideAxis::Vertical ? "horizontal" : "vertical";
        allWidgets = renderer_.CollectSimilarityIndicatorWidgets(axis);
        for (const WidgetLayout* widget : allWidgets) {
            if (renderer_.IsWidgetAffectedByGuide(*widget, guide)) {
                affectedWidgets.push_back(widget);
            }
        }
    }
    if (affectedWidgets.empty()) {
        return;
    }

    std::unordered_map<WidgetClass, std::vector<const WidgetLayout*>> widgetsByClass;
    widgetsByClass.reserve(allWidgets.size());
    for (const WidgetLayout* widget : allWidgets) {
        if (widget->widget == nullptr) {
            continue;
        }
        widgetsByClass[widget->widget->Class()].push_back(widget);
    }

    std::unordered_set<const WidgetLayout*> visibleWidgets;
    visibleWidgets.reserve(allWidgets.size());
    std::unordered_map<const WidgetLayout*, SimilarityTypeKey> exactTypeByWidget;
    exactTypeByWidget.reserve(allWidgets.size());
    for (const WidgetLayout* affected : affectedWidgets) {
        const int affectedExtent = renderer_.WidgetExtentForAxis(*affected, axis);
        if (affectedExtent <= 0 || affected->widget == nullptr) {
            continue;
        }
        const SimilarityTypeKey typeKey{affected->widget->Class(), affectedExtent};
        bool hasExactMatch = false;
        const auto classIt = widgetsByClass.find(typeKey.widgetClass);
        if (classIt == widgetsByClass.end()) {
            continue;
        }
        for (const WidgetLayout* candidate : classIt->second) {
            if (candidate == affected || candidate->widget == nullptr ||
                candidate->widget->Class() != affected->widget->Class()) {
                continue;
            }
            const int candidateExtent = renderer_.WidgetExtentForAxis(*candidate, axis);
            if (candidateExtent <= 0 || std::abs(candidateExtent - affectedExtent) > threshold) {
                continue;
            }
            visibleWidgets.insert(affected);
            visibleWidgets.insert(candidate);
            if (candidateExtent == affectedExtent) {
                hasExactMatch = true;
                exactTypeByWidget.try_emplace(candidate, typeKey);
            }
        }
        if (hasExactMatch) {
            exactTypeByWidget.try_emplace(affected, typeKey);
        }
    }

    std::unordered_map<SimilarityTypeKey, int, SimilarityTypeKeyHash> exactTypeOrdinals;
    exactTypeOrdinals.reserve(exactTypeByWidget.size());
    int nextOrdinal = 1;
    for (const WidgetLayout* widget : allWidgets) {
        if (!visibleWidgets.contains(widget)) {
            continue;
        }
        const auto exactIt = exactTypeByWidget.find(widget);
        if (exactIt == exactTypeByWidget.end() || exactTypeOrdinals.contains(exactIt->second)) {
            continue;
        }
        exactTypeOrdinals[exactIt->second] = nextOrdinal++;
    }

    std::vector<SimilarityIndicator> indicators;
    indicators.reserve(visibleWidgets.size());
    for (const WidgetLayout* widget : allWidgets) {
        if (!visibleWidgets.contains(widget)) {
            continue;
        }
        const auto exactIt = exactTypeByWidget.find(widget);
        const int exactTypeOrdinal = exactIt == exactTypeByWidget.end() ? 0 : exactTypeOrdinals[exactIt->second];
        indicators.push_back(SimilarityIndicator{
            axis,
            widget->rect,
            exactTypeOrdinal,
        });
    }
    if (indicators.empty()) {
        return;
    }

    for (const auto& entry : exactTypeOrdinals) {
        renderer_.WriteTrace("renderer:layout_similarity_group axis=\"" + std::string(axisLabel) +
                             "\" class=" + std::to_string(static_cast<int>(entry.first.widgetClass)) + " extent=" +
                             std::to_string(entry.first.extent) + " ordinal=" + std::to_string(entry.second));
    }

    const RenderColorId color = RenderColorId::LayoutGuide;
    const int inset = std::max(2, renderer_.ScaleLogical(4));
    const int cap = std::max(3, renderer_.ScaleLogical(4));
    const int offset = std::max(4, renderer_.ScaleLogical(6));
    const int notchDepth = std::max(3, renderer_.ScaleLogical(4));
    const int notchSpacing = std::max(3, renderer_.ScaleLogical(4));

    for (const SimilarityIndicator& indicator : indicators) {
        const RenderRect& rect = indicator.rect;
        if (indicator.axis == LayoutGuideAxis::Vertical) {
            const int y = rect.top + offset;
            const int left = rect.left + inset;
            const int right = rect.right - inset;
            const RenderStroke stroke = RenderStroke::Solid(color);
            renderer_.DrawSolidLine(RenderPoint{left, y}, RenderPoint{right, y}, stroke);
            renderer_.DrawSolidLine(RenderPoint{left + cap, y - cap}, RenderPoint{left, y}, stroke);
            renderer_.DrawSolidLine(RenderPoint{left, y}, RenderPoint{left + cap, y + cap + 1}, stroke);
            renderer_.DrawSolidLine(RenderPoint{right - cap, y - cap}, RenderPoint{right, y}, stroke);
            renderer_.DrawSolidLine(RenderPoint{right, y}, RenderPoint{right - cap, y + cap + 1}, stroke);
            if (indicator.exactTypeOrdinal > 0) {
                const int cx = left + std::max(0, (right - left) / 2);
                const int count = indicator.exactTypeOrdinal;
                const int totalWidth = (count - 1) * notchSpacing;
                int notchX = cx - (totalWidth / 2);
                for (int i = 0; i < count; ++i) {
                    renderer_.DrawSolidLine(
                        RenderPoint{notchX, y - notchDepth}, RenderPoint{notchX, y + notchDepth + 1}, stroke);
                    notchX += notchSpacing;
                }
            }
        } else {
            const int x = rect.left + offset;
            const int top = rect.top + inset;
            const int bottom = rect.bottom - inset;
            const RenderStroke stroke = RenderStroke::Solid(color);
            renderer_.DrawSolidLine(RenderPoint{x, top}, RenderPoint{x, bottom}, stroke);
            renderer_.DrawSolidLine(RenderPoint{x - cap, top + cap}, RenderPoint{x, top}, stroke);
            renderer_.DrawSolidLine(RenderPoint{x, top}, RenderPoint{x + cap + 1, top + cap}, stroke);
            renderer_.DrawSolidLine(RenderPoint{x - cap, bottom - cap}, RenderPoint{x, bottom}, stroke);
            renderer_.DrawSolidLine(RenderPoint{x, bottom}, RenderPoint{x + cap + 1, bottom - cap}, stroke);
            if (indicator.exactTypeOrdinal > 0) {
                const int cy = top + std::max(0, (bottom - top) / 2);
                const int count = indicator.exactTypeOrdinal;
                const int totalHeight = (count - 1) * notchSpacing;
                int notchY = cy - (totalHeight / 2);
                for (int i = 0; i < count; ++i) {
                    renderer_.DrawSolidLine(
                        RenderPoint{x - notchDepth, notchY}, RenderPoint{x + notchDepth + 1, notchY}, stroke);
                    notchY += notchSpacing;
                }
            }
        }
    }
}

std::optional<RenderPoint> DashboardLayoutEditOverlayRenderer::ContainerChildReorderOffsetForRect(
    const DashboardOverlayState& overlayState, const RenderRect& rect) const {
    if (!overlayState.activeContainerChildReorderDrag.has_value()) {
        return std::nullopt;
    }
    const ContainerChildReorderOverlayState& drag = *overlayState.activeContainerChildReorderDrag;
    if (drag.currentIndex < 0 || drag.currentIndex >= static_cast<int>(drag.childRects.size())) {
        return std::nullopt;
    }
    const RenderRect& childRect = drag.childRects[static_cast<size_t>(drag.currentIndex)];
    if (!(rect.left >= childRect.left && rect.top >= childRect.top && rect.right <= childRect.right &&
            rect.bottom <= childRect.bottom)) {
        return std::nullopt;
    }

    const int childStart = drag.horizontal ? childRect.left : childRect.top;
    const int offset = drag.mouseCoordinate - drag.dragOffset - childStart;
    return drag.horizontal ? RenderPoint{offset, 0} : RenderPoint{0, offset};
}

RenderRect DashboardLayoutEditOverlayRenderer::ApplyContainerChildReorderOffset(const RenderRect& rect) const {
    if (const auto offset = ContainerChildReorderOffsetForRect(*renderer_.activeOverlayState_, rect);
        offset.has_value()) {
        return OffsetRect(rect, offset->x, offset->y);
    }
    return rect;
}

RenderPoint DashboardLayoutEditOverlayRenderer::ApplyContainerChildReorderOffset(
    RenderPoint point, const RenderRect& sourceRect) const {
    if (const auto offset = ContainerChildReorderOffsetForRect(*renderer_.activeOverlayState_, sourceRect);
        offset.has_value()) {
        point.x += offset->x;
        point.y += offset->y;
    }
    return point;
}

bool DashboardLayoutEditOverlayRenderer::ShouldSkipBaseWidget(
    const DashboardOverlayState& overlayState, const RenderRect& rect) const {
    return ContainerChildReorderOffsetForRect(overlayState, rect).has_value();
}

bool DashboardLayoutEditOverlayRenderer::ShouldSkipForContainerChildReorder(const RenderRect& rect) const {
    return renderer_.activeOverlayState_ != nullptr &&
           ContainerChildReorderOffsetForRect(*renderer_.activeOverlayState_, rect).has_value();
}

void DashboardLayoutEditOverlayRenderer::DrawContainerChildReorderOverlay(const MetricSource& metrics) {
    if (renderer_.activeOverlayState_ == nullptr ||
        !renderer_.activeOverlayState_->activeContainerChildReorderDrag.has_value() ||
        renderer_.d2dActiveRenderTarget_ == nullptr) {
        return;
    }

    const ContainerChildReorderOverlayState& drag = *renderer_.activeOverlayState_->activeContainerChildReorderDrag;
    if (drag.currentIndex < 0 || drag.currentIndex >= static_cast<int>(drag.childRects.size())) {
        return;
    }

    const RenderRect& childRect = drag.childRects[static_cast<size_t>(drag.currentIndex)];
    const int childStart = drag.horizontal ? childRect.left : childRect.top;
    const int offset = drag.mouseCoordinate - drag.dragOffset - childStart;

    D2D1_MATRIX_3X2_F previousTransform{};
    renderer_.d2dActiveRenderTarget_->GetTransform(&previousTransform);
    const D2D1_MATRIX_3X2_F translation = drag.horizontal
                                              ? D2D1::Matrix3x2F::Translation(static_cast<float>(offset), 0.0f)
                                              : D2D1::Matrix3x2F::Translation(0.0f, static_cast<float>(offset));
    renderer_.d2dActiveRenderTarget_->SetTransform(translation * previousTransform);
    const bool previousDynamicRegistration = layoutResolver_.dynamicAnchorRegistrationEnabled_;
    layoutResolver_.dynamicAnchorRegistrationEnabled_ = false;
    for (const auto& card : layoutResolver_.resolvedLayout_.cards) {
        if (ShouldSkipForContainerChildReorder(card.chrome.rect)) {
            renderer_.DrawResolvedWidget(card.chrome, metrics);
        }
        for (const auto& widget : card.widgets) {
            if (ShouldSkipForContainerChildReorder(widget.rect)) {
                renderer_.DrawResolvedWidget(widget, metrics);
            }
        }
    }
    renderer_.DrawSolidRect(childRect,
        RenderStroke::Dotted(RenderColorId::ActiveEdit, static_cast<float>((std::max)(2, renderer_.ScaleLogical(2)))));
    layoutResolver_.dynamicAnchorRegistrationEnabled_ = previousDynamicRegistration;
    renderer_.d2dActiveRenderTarget_->SetTransform(previousTransform);
}
