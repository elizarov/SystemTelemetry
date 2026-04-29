#include "dashboard_renderer/impl/layout_resolver.h"

#include <algorithm>
#include <functional>
#include <map>

#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_model/layout_edit_parameter_metadata.h"

namespace {

bool ContainsCardReference(const std::vector<std::string>& stack, const std::string& cardId) {
    return std::find(stack.begin(), stack.end(), cardId) != stack.end();
}

std::string FormatRect(const RenderRect& rect) {
    return "rect=(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," + std::to_string(rect.right) +
           "," + std::to_string(rect.bottom) + ")";
}

RenderRect MakeSquareAnchorRect(int centerX, int centerY, int size) {
    const int clampedSize = (std::max)(4, size);
    const int radius = clampedSize / 2;
    return RenderRect{
        centerX - radius, centerY - radius, centerX - radius + clampedSize, centerY - radius + clampedSize};
}

RenderRect MakeCenteredRect(int centerX, int centerY, int width, int height) {
    return RenderRect{
        centerX - width / 2, centerY - height / 2, centerX - width / 2 + width, centerY - height / 2 + height};
}

bool RectsOverlap(const RenderRect& lhs, const RenderRect& rhs) {
    return lhs.left < rhs.right && lhs.right > rhs.left && lhs.top < rhs.bottom && lhs.bottom > rhs.top;
}

bool OverlapsAnyRect(const RenderRect& rect, const std::vector<RenderRect>& occupiedRects) {
    return std::any_of(occupiedRects.begin(), occupiedRects.end(), [&](const RenderRect& occupied) {
        return RectsOverlap(rect, occupied);
    });
}

std::vector<int> AlternatingOffsets(int step, int negativeLimit, int positiveLimit) {
    std::vector<int> offsets{0};
    for (int distance = step; distance <= (std::max)(negativeLimit, positiveLimit); distance += step) {
        if (distance <= negativeLimit) {
            offsets.push_back(-distance);
        }
        if (distance <= positiveLimit) {
            offsets.push_back(distance);
        }
    }
    return offsets;
}

int AnchorHitInset(const LayoutEditAnchorRegion& anchor) {
    return (std::max)(0, anchor.anchorRect.left - anchor.anchorHitRect.left);
}

bool IsContainerChildOrderAnchor(const LayoutEditAnchorRegion& anchor) {
    return std::holds_alternative<LayoutContainerChildOrderEditKey>(anchor.key.subject);
}

RenderRect ResolveNonOverlappingEdgeAnchorRect(const RenderRect& childRect,
    const RenderRect& preferredRect,
    bool horizontal,
    int handleInset,
    int hitInset,
    int collisionStep,
    const std::vector<RenderRect>& occupiedAnchorHitRects) {
    const int handleWidth = preferredRect.Width();
    const int handleHeight = preferredRect.Height();
    const RenderPoint preferredCenter = preferredRect.Center();
    const auto candidateFree = [&](const RenderRect& candidate) {
        return !OverlapsAnyRect(candidate.Inflate(hitInset, hitInset), occupiedAnchorHitRects);
    };
    if (candidateFree(preferredRect)) {
        return preferredRect;
    }

    if (horizontal) {
        const int minCenterX = childRect.left + handleWidth / 2 + handleInset;
        const int maxCenterX = childRect.right - handleWidth / 2 - handleInset;
        if (minCenterX <= maxCenterX) {
            const int negativeLimit = preferredCenter.x - minCenterX;
            const int positiveLimit = maxCenterX - preferredCenter.x;
            const auto tryHorizontalLane = [&](int centerY) -> std::optional<RenderRect> {
                for (int offset : AlternatingOffsets(collisionStep, negativeLimit, positiveLimit)) {
                    const RenderRect candidate =
                        MakeCenteredRect(preferredCenter.x + offset, centerY, handleWidth, handleHeight);
                    if (candidateFree(candidate)) {
                        return candidate;
                    }
                }
                return std::nullopt;
            };
            if (const auto candidate = tryHorizontalLane(preferredCenter.y); candidate.has_value()) {
                return *candidate;
            }

            const int maxCenterY = childRect.bottom - handleHeight / 2 - handleInset;
            for (int laneOffset = collisionStep; preferredCenter.y + laneOffset <= maxCenterY;
                laneOffset += collisionStep) {
                if (const auto candidate = tryHorizontalLane(preferredCenter.y + laneOffset); candidate.has_value()) {
                    return *candidate;
                }
            }
        }
    } else {
        const int minCenterY = childRect.top + handleHeight / 2 + handleInset;
        const int maxCenterY = childRect.bottom - handleHeight / 2 - handleInset;
        if (minCenterY <= maxCenterY) {
            const int negativeLimit = preferredCenter.y - minCenterY;
            const int positiveLimit = maxCenterY - preferredCenter.y;
            const auto tryVerticalLane = [&](int centerX) -> std::optional<RenderRect> {
                for (int offset : AlternatingOffsets(collisionStep, negativeLimit, positiveLimit)) {
                    const RenderRect candidate =
                        MakeCenteredRect(centerX, preferredCenter.y + offset, handleWidth, handleHeight);
                    if (candidateFree(candidate)) {
                        return candidate;
                    }
                }
                return std::nullopt;
            };
            if (const auto candidate = tryVerticalLane(preferredCenter.x); candidate.has_value()) {
                return *candidate;
            }

            const int minCenterX = childRect.left + handleWidth / 2 + handleInset;
            for (int laneOffset = collisionStep; preferredCenter.x - laneOffset >= minCenterX;
                laneOffset += collisionStep) {
                if (const auto candidate = tryVerticalLane(preferredCenter.x - laneOffset); candidate.has_value()) {
                    return *candidate;
                }
            }
        }
    }

    return preferredRect;
}

bool IsColorEditParameter(LayoutEditParameter parameter) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
    return descriptor.has_value() && descriptor->valueFormat == configschema::ValueFormat::ColorHex;
}

RenderRect TextAnchorRectForShape(const DashboardRenderer& renderer, const RenderRect& textRect, AnchorShape shape) {
    const int anchorSize = std::max(4, renderer.ScaleLogical(6));
    const int anchorHalf = anchorSize / 2;
    if (shape == AnchorShape::Wedge) {
        const int outsideLeft = std::max(1, renderer.ScaleLogical(2));
        const int outsideTop = std::max(1, renderer.ScaleLogical(1));
        const int insideWidth = std::max(4, renderer.ScaleLogical(5));
        const int insideHeight = std::max(5, renderer.ScaleLogical(6));
        return RenderRect{textRect.left - outsideLeft,
            textRect.top - outsideTop,
            textRect.left + insideWidth,
            textRect.top - outsideTop + insideHeight};
    }

    const int anchorCenterX = textRect.right;
    const int anchorCenterY = textRect.top;
    return RenderRect{anchorCenterX - anchorHalf,
        anchorCenterY - anchorHalf,
        anchorCenterX - anchorHalf + anchorSize,
        anchorCenterY - anchorHalf + anchorSize};
}

}  // namespace

DashboardLayoutResolver::DashboardLayoutResolver(DashboardRenderer& renderer) : renderer_(renderer) {}

void DashboardLayoutResolver::Clear() {
    resolvedLayout_ = {};
    layoutEditGuides_.clear();
    containerChildReorderTargets_.clear();
    widgetEditGuides_.clear();
    gapEditAnchors_.clear();
    staticEditableAnchorRegions_.clear();
    dynamicEditableAnchorRegions_.clear();
    staticColorEditRegions_.clear();
    dynamicColorEditRegions_.clear();
    dynamicAnchorRegistrationEnabled_ = false;
    parsedWidgetInfoCache_.clear();
}

void DashboardLayoutResolver::ClearDynamicEditArtifacts() {
    dynamicEditableAnchorRegions_.clear();
    dynamicColorEditRegions_.clear();
}

void DashboardLayoutResolver::RegisterEditableAnchorRegion(
    std::vector<LayoutEditAnchorRegion>& regions, const LayoutEditAnchorRegistration& registration) {
    if (registration.anchorRect.right <= registration.anchorRect.left ||
        registration.anchorRect.bottom <= registration.anchorRect.top) {
        return;
    }
    LayoutEditAnchorRegion region;
    region.key = registration.key;
    region.targetRect = registration.targetRect;
    region.anchorRect = registration.anchorRect;
    region.shape = registration.shape;
    const int anchorHitInset = registration.shape == AnchorShape::Wedge ? std::max(1, renderer_.ScaleLogical(2))
                                                                        : std::max(4, renderer_.ScaleLogical(5));
    region.anchorHitPadding = anchorHitInset;
    region.anchorHitRect = RenderRect{region.anchorRect.left - anchorHitInset,
        region.anchorRect.top - anchorHitInset,
        region.anchorRect.right + anchorHitInset,
        region.anchorRect.bottom + anchorHitInset};
    if (registration.drag.has_value()) {
        region.dragAxis = registration.drag->axis;
        region.dragMode = registration.drag->mode;
        region.dragOrigin = registration.drag->origin;
        region.dragScale = registration.drag->scale;
        region.draggable = true;
    } else {
        region.draggable = false;
    }
    region.showWhenWidgetHovered = registration.visibility == LayoutEditAnchorVisibility::WhenWidgetHovered;
    region.drawTargetOutline = registration.targetOutline == LayoutEditTargetOutline::Visible;
    region.value = registration.value;
    regions.push_back(std::move(region));
}

void DashboardLayoutResolver::RegisterStaticEditAnchor(LayoutEditAnchorRegistration registration) {
    RegisterEditableAnchorRegion(staticEditableAnchorRegions_, registration);
}

void DashboardLayoutResolver::RegisterDynamicEditAnchor(LayoutEditAnchorRegistration registration) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterEditableAnchorRegion(dynamicEditableAnchorRegions_, registration);
}

void DashboardLayoutResolver::RegisterStaticCornerEditAnchor(
    const LayoutEditAnchorKey& key, const RenderRect& targetRect) {
    const int anchorSize = std::max(6, renderer_.Renderer().ScaleLogical(8));
    RegisterStaticEditAnchor(LayoutEditAnchorRegistration{.key = key,
        .targetRect = targetRect,
        .anchorRect =
            RenderRect{targetRect.left, targetRect.top, targetRect.left + anchorSize, targetRect.top + anchorSize},
        .shape = AnchorShape::Wedge,
        .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
        .targetOutline = LayoutEditTargetOutline::Hidden});
}

void DashboardLayoutResolver::RegisterDynamicCornerEditAnchor(
    const LayoutEditAnchorKey& key, const RenderRect& targetRect) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    const int anchorSize = std::max(6, renderer_.Renderer().ScaleLogical(8));
    RegisterDynamicEditAnchor(LayoutEditAnchorRegistration{.key = key,
        .targetRect = targetRect,
        .anchorRect =
            RenderRect{targetRect.left, targetRect.top, targetRect.left + anchorSize, targetRect.top + anchorSize},
        .shape = AnchorShape::Wedge,
        .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
        .targetOutline = LayoutEditTargetOutline::Hidden});
}

void DashboardLayoutResolver::RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
    const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const LayoutEditAnchorBinding& editable,
    LayoutEditTargetOutline targetOutline) {
    if (text.empty()) {
        return;
    }

    const TextLayoutResult result = renderer_.Renderer().MeasureTextBlock(rect, text, style, options);
    const RenderRect anchorRect = TextAnchorRectForShape(renderer_, result.textRect, editable.shape);
    const RenderPoint anchorOrigin = anchorRect.Center();
    LayoutEditAnchorRegistration registration;
    registration.key = editable.key;
    registration.targetRect = result.textRect;
    registration.anchorRect = anchorRect;
    registration.shape = editable.shape;
    registration.value = editable.value;
    registration.targetOutline = targetOutline;
    if (editable.drag.has_value()) {
        registration.drag =
            LayoutEditAnchorDrag{editable.drag->axis, editable.drag->mode, anchorOrigin, editable.drag->scale};
    }
    RegisterEditableAnchorRegion(regions, registration);
}

void DashboardLayoutResolver::RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
    const TextLayoutResult& layoutResult,
    const LayoutEditAnchorBinding& editable,
    LayoutEditTargetOutline targetOutline) {
    const RenderRect& textRect = layoutResult.textRect;
    if (textRect.right <= textRect.left || textRect.bottom <= textRect.top) {
        return;
    }

    const RenderRect anchorRect = TextAnchorRectForShape(renderer_, textRect, editable.shape);
    const RenderPoint anchorOrigin = anchorRect.Center();
    LayoutEditAnchorRegistration registration;
    registration.key = editable.key;
    registration.targetRect = textRect;
    registration.anchorRect = anchorRect;
    registration.shape = editable.shape;
    registration.value = editable.value;
    registration.targetOutline = targetOutline;
    if (editable.drag.has_value()) {
        registration.drag =
            LayoutEditAnchorDrag{editable.drag->axis, editable.drag->mode, anchorOrigin, editable.drag->scale};
    }
    RegisterEditableAnchorRegion(regions, registration);
}

void DashboardLayoutResolver::RegisterStaticTextAnchor(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const LayoutEditAnchorBinding& editable,
    std::optional<LayoutEditParameter> colorParameter,
    LayoutEditTargetOutline targetOutline) {
    RegisterTextAnchor(staticEditableAnchorRegions_, rect, text, style, options, editable, targetOutline);
    if (colorParameter.has_value()) {
        RegisterStaticColorEditRegion(
            *colorParameter, renderer_.Renderer().MeasureTextBlock(rect, text, style, options).textRect);
    }
}

void DashboardLayoutResolver::RegisterDynamicTextAnchor(const TextLayoutResult& layoutResult,
    const LayoutEditAnchorBinding& editable,
    std::optional<LayoutEditParameter> colorParameter,
    LayoutEditTargetOutline targetOutline) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(dynamicEditableAnchorRegions_, layoutResult, editable, targetOutline);
    if (colorParameter.has_value()) {
        RegisterDynamicColorEditRegion(*colorParameter, layoutResult.textRect);
    }
}

void DashboardLayoutResolver::RegisterDynamicTextAnchor(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const LayoutEditAnchorBinding& editable,
    std::optional<LayoutEditParameter> colorParameter,
    LayoutEditTargetOutline targetOutline) {
    if (!dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(dynamicEditableAnchorRegions_, rect, text, style, options, editable, targetOutline);
    if (colorParameter.has_value()) {
        RegisterDynamicColorEditRegion(
            *colorParameter, renderer_.Renderer().MeasureTextBlock(rect, text, style, options).textRect);
    }
}

void DashboardLayoutResolver::RegisterStaticColorEditRegion(
    LayoutEditParameter parameter, const RenderRect& targetRect) {
    if (!IsColorEditParameter(parameter) || targetRect.IsEmpty()) {
        return;
    }
    staticColorEditRegions_.push_back(LayoutEditColorRegion{parameter, targetRect});
}

void DashboardLayoutResolver::RegisterDynamicColorEditRegion(
    LayoutEditParameter parameter, const RenderRect& targetRect) {
    if (!dynamicAnchorRegistrationEnabled_ || !IsColorEditParameter(parameter) || targetRect.IsEmpty()) {
        return;
    }
    dynamicColorEditRegions_.push_back(LayoutEditColorRegion{parameter, targetRect});
}

void DashboardLayoutResolver::ResolveContainerAnchorCollisions() {
    const int collisionStep = (std::max)(4, renderer_.ScaleLogical(4));
    std::vector<RenderRect> occupiedAnchorHitRects;
    occupiedAnchorHitRects.reserve(staticEditableAnchorRegions_.size() + dynamicEditableAnchorRegions_.size());
    for (const LayoutEditAnchorRegion& anchor : staticEditableAnchorRegions_) {
        if (!IsContainerChildOrderAnchor(anchor)) {
            occupiedAnchorHitRects.push_back(anchor.anchorHitRect);
        }
    }
    for (const LayoutEditAnchorRegion& anchor : dynamicEditableAnchorRegions_) {
        occupiedAnchorHitRects.push_back(anchor.anchorHitRect);
    }

    for (LayoutEditAnchorRegion& anchor : staticEditableAnchorRegions_) {
        if (!IsContainerChildOrderAnchor(anchor)) {
            continue;
        }
        const bool horizontal = anchor.shape == AnchorShape::HorizontalReorder;
        const int hitInset = AnchorHitInset(anchor);
        const RenderRect resolvedRect = ResolveNonOverlappingEdgeAnchorRect(
            anchor.targetRect, anchor.anchorRect, horizontal, 0, hitInset, collisionStep, occupiedAnchorHitRects);
        anchor.anchorRect = resolvedRect;
        anchor.anchorHitRect = resolvedRect.Inflate(hitInset, hitInset);
        if (anchor.draggable) {
            anchor.dragOrigin = resolvedRect.Center();
        }
        occupiedAnchorHitRects.push_back(anchor.anchorHitRect);
    }
}

void DashboardLayoutResolver::ResolveDynamicEditArtifactCollisions() {
    ResolveContainerAnchorCollisions();
}

void DashboardLayoutResolver::RegisterWidgetEditGuide(LayoutEditWidgetGuide guide) {
    widgetEditGuides_.push_back(std::move(guide));
}

void DashboardLayoutResolver::ResolveNodeWidgets(DashboardRenderer& renderer,
    const LayoutNodeConfig& node,
    const RenderRect& rect,
    std::vector<WidgetLayout>& widgets,
    bool instantiateWidgets) {
    std::vector<std::string> cardReferenceStack;
    ResolveNodeWidgetsInternal(renderer, node, rect, widgets, cardReferenceStack, "", "", {}, instantiateWidgets);
}

void DashboardLayoutResolver::BuildWidgetEditGuides(DashboardRenderer& renderer) {
    widgetEditGuides_.clear();
    for (const auto& card : resolvedLayout_.cards) {
        if (card.chrome.widget != nullptr) {
            card.chrome.widget->BuildEditGuides(renderer, card.chrome);
        }

        for (const auto& widget : card.widgets) {
            if (widget.widget != nullptr) {
                widget.widget->BuildEditGuides(renderer, widget);
            }
        }
    }
}

void DashboardLayoutResolver::BuildStaticEditableAnchors(DashboardRenderer& renderer) {
    staticEditableAnchorRegions_.clear();
    staticColorEditRegions_.clear();
    for (const auto& card : resolvedLayout_.cards) {
        if (card.chrome.widget != nullptr) {
            card.chrome.widget->BuildStaticAnchors(renderer, card.chrome);
        }
        for (const auto& widget : card.widgets) {
            if (widget.widget != nullptr) {
                widget.widget->BuildStaticAnchors(renderer, widget);
            }
        }
    }

    for (const ContainerChildReorderTarget& target : containerChildReorderTargets_) {
        if (target.childRects.size() < 2) {
            continue;
        }

        const bool horizontal = target.horizontal;
        const int handleWidth = (std::max)(6, renderer.ScaleLogical(horizontal ? 12 : 8));
        const int handleHeight = (std::max)(6, renderer.ScaleLogical(horizontal ? 8 : 12));
        const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
        const int handleInset = (std::max)(2, renderer.ScaleLogical(3));
        const int collisionStep = (std::max)(4, renderer.ScaleLogical(4));
        const LayoutEditWidgetIdentity widgetIdentity =
            target.editCardId.empty()
                ? LayoutEditWidgetIdentity{"", "", target.nodePath, LayoutEditWidgetIdentity::Kind::DashboardChrome}
                : LayoutEditWidgetIdentity{
                      target.renderCardId, target.editCardId, target.nodePath, LayoutEditWidgetIdentity::Kind::Widget};
        std::vector<RenderRect> occupiedAnchorHitRects;
        occupiedAnchorHitRects.reserve(staticEditableAnchorRegions_.size() + target.childRects.size());
        for (const LayoutEditAnchorRegion& region : staticEditableAnchorRegions_) {
            occupiedAnchorHitRects.push_back(region.anchorHitRect);
        }

        for (size_t i = 0; i < target.childRects.size(); ++i) {
            const RenderRect& childRect = target.childRects[i];
            if (childRect.IsEmpty()) {
                continue;
            }
            const int centerX =
                horizontal ? childRect.left + childRect.Width() / 2 : childRect.right - (handleWidth / 2) - handleInset;
            const int centerY =
                horizontal ? childRect.top + (handleHeight / 2) + handleInset : childRect.top + childRect.Height() / 2;
            const RenderRect anchorRect = ResolveNonOverlappingEdgeAnchorRect(childRect,
                MakeCenteredRect(centerX, centerY, handleWidth, handleHeight),
                horizontal,
                handleInset,
                hitInset,
                collisionStep,
                occupiedAnchorHitRects);
            RegisterStaticEditAnchor(
                LayoutEditAnchorRegistration{.key = LayoutEditAnchorKey{widgetIdentity,
                                                 LayoutContainerChildOrderEditKey{target.editCardId, target.nodePath},
                                                 static_cast<int>(i)},
                    .targetRect = childRect,
                    .anchorRect = anchorRect,
                    .shape = horizontal ? AnchorShape::HorizontalReorder : AnchorShape::VerticalReorder,
                    .drag = LayoutEditAnchorDrag::AxisDelta(
                        horizontal ? AnchorDragAxis::Horizontal : AnchorDragAxis::Vertical, anchorRect.Center()),
                    .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
                    .targetOutline = LayoutEditTargetOutline::Hidden});
            staticEditableAnchorRegions_.back().anchorHitRect = anchorRect.Inflate(hitInset, hitInset);
            occupiedAnchorHitRects.push_back(staticEditableAnchorRegions_.back().anchorHitRect);
        }
    }
}

void DashboardLayoutResolver::AddLayoutEditGuide(DashboardRenderer& renderer,
    const LayoutNodeConfig& node,
    const RenderRect& rect,
    const std::vector<RenderRect>& childRects,
    int gap,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath) {
    if (!DashboardRenderer::IsContainerNode(node) || childRects.size() < 2) {
        return;
    }

    const bool horizontal = node.name == "columns";
    containerChildReorderTargets_.push_back(
        ContainerChildReorderTarget{renderCardId, editCardId, nodePath, horizontal, childRects});
    const LayoutEditWidgetIdentity gapWidgetIdentity =
        renderCardId.empty()
            ? LayoutEditWidgetIdentity{"", "", {}, LayoutEditWidgetIdentity::Kind::DashboardChrome}
            : LayoutEditWidgetIdentity{renderCardId, renderCardId, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
    const DashboardRenderer::LayoutEditParameter gapParameter =
        renderCardId.empty() ? (horizontal ? DashboardRenderer::LayoutEditParameter::DashboardColumnGap
                                           : DashboardRenderer::LayoutEditParameter::DashboardRowGap)
                             : (horizontal ? DashboardRenderer::LayoutEditParameter::CardColumnGap
                                           : DashboardRenderer::LayoutEditParameter::CardRowGap);
    const bool gapAnchorAlreadyRegistered =
        std::any_of(gapEditAnchors_.begin(), gapEditAnchors_.end(), [&](const auto& anchor) {
            return anchor.key.parameter == gapParameter && anchor.key.widget.kind == gapWidgetIdentity.kind &&
                   anchor.key.widget.renderCardId == gapWidgetIdentity.renderCardId &&
                   anchor.key.widget.editCardId == gapWidgetIdentity.editCardId;
        });
    if (!gapAnchorAlreadyRegistered) {
        LayoutEditGapAnchor anchor;
        anchor.axis = horizontal ? LayoutGuideAxis::Horizontal : LayoutGuideAxis::Vertical;
        anchor.key.widget = gapWidgetIdentity;
        anchor.key.parameter = gapParameter;
        anchor.key.nodePath = nodePath;
        const RenderRect& firstGapLead = childRects.front();
        const RenderRect& firstGapTrail = childRects[1];
        const int handleSize = (std::max)(4, renderer.ScaleLogical(6));
        const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
        if (horizontal) {
            anchor.drawStart = RenderPoint{firstGapLead.right, rect.top};
            anchor.drawEnd = RenderPoint{firstGapTrail.left, rect.top};
            anchor.dragAxis = AnchorDragAxis::Horizontal;
            anchor.handleRect = MakeSquareAnchorRect(anchor.drawEnd.x, anchor.drawEnd.y, handleSize);
            anchor.value = renderCardId.empty() ? renderer.Config().layout.dashboard.columnGap
                                                : renderer.Config().layout.cardStyle.columnGap;
        } else {
            anchor.drawStart = RenderPoint{rect.left, firstGapLead.bottom};
            anchor.drawEnd = RenderPoint{rect.left, firstGapTrail.top};
            anchor.dragAxis = AnchorDragAxis::Vertical;
            anchor.handleRect = MakeSquareAnchorRect(anchor.drawEnd.x, anchor.drawEnd.y, handleSize);
            anchor.value = renderCardId.empty() ? renderer.Config().layout.dashboard.rowGap
                                                : renderer.Config().layout.cardStyle.rowGap;
        }
        anchor.hitRect = anchor.handleRect.Inflate(hitInset, hitInset);
        gapEditAnchors_.push_back(std::move(anchor));
    }

    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    std::vector<bool> childFixedExtents;
    childFixedExtents.reserve(node.children.size());
    for (const auto& child : node.children) {
        const DashboardLayoutResolver::ParsedWidgetInfo* childWidget = FindParsedWidgetInfo(renderer, child);
        childFixedExtents.push_back(!horizontal && childWidget != nullptr &&
                                    (childWidget->fixedPreferredHeightInRows || childWidget->verticalSpring));
    }
    for (size_t i = 0; i + 1 < childRects.size(); ++i) {
        if (!horizontal && (childFixedExtents[i] || childFixedExtents[i + 1])) {
            continue;
        }
        LayoutEditGuide guide;
        guide.axis = horizontal ? LayoutGuideAxis::Vertical : LayoutGuideAxis::Horizontal;
        guide.renderCardId = renderCardId;
        guide.editCardId = editCardId;
        guide.nodePath = nodePath;
        guide.separatorIndex = i;
        guide.containerRect = rect;
        guide.gap = gap;
        guide.childExtents.reserve(childRects.size());
        guide.childFixedExtents = childFixedExtents;
        guide.childRects = childRects;
        for (const RenderRect& childRect : childRects) {
            guide.childExtents.push_back(
                horizontal ? (childRect.right - childRect.left) : (childRect.bottom - childRect.top));
        }

        if (horizontal) {
            const int x = childRects[i].right + (std::max)(0, gap / 2);
            guide.lineRect = RenderRect{x, rect.top, x + 1, rect.bottom};
            guide.hitRect = RenderRect{x - hitInset, rect.top, x + hitInset + 1, rect.bottom};
        } else {
            const int y = childRects[i].bottom + (std::max)(0, gap / 2);
            guide.lineRect = RenderRect{rect.left, y, rect.right, y + 1};
            guide.hitRect = RenderRect{rect.left, y - hitInset, rect.right, y + hitInset + 1};
        }
        layoutEditGuides_.push_back(std::move(guide));
    }
}

int DashboardLayoutResolver::PreferredNodeHeight(
    const DashboardRenderer& renderer, const LayoutNodeConfig& node, int) const {
    if (node.name == "rows") {
        int total = 0;
        for (size_t i = 0; i < node.children.size(); ++i) {
            total += PreferredNodeHeight(renderer, node.children[i], 0);
            if (i + 1 < node.children.size()) {
                total += renderer.ScaleLogical(renderer.config_.layout.cardStyle.rowGap);
            }
        }
        renderer.WriteTrace(
            "renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(total));
        return total;
    }
    if (node.name == "columns") {
        int tallest = 0;
        for (const auto& child : node.children) {
            tallest = (std::max)(tallest, PreferredNodeHeight(renderer, child, 0));
        }
        renderer.WriteTrace(
            "renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(tallest));
        return tallest;
    }
    const ParsedWidgetInfo* widget = FindParsedWidgetInfo(renderer, node);
    const int preferredHeight = widget != nullptr ? widget->preferredHeight : 0;
    renderer.WriteTrace(
        "renderer:layout_preferred_height node=\"" + node.name + "\" value=" + std::to_string(preferredHeight));
    return preferredHeight;
}

const DashboardLayoutResolver::ParsedWidgetInfo* DashboardLayoutResolver::FindParsedWidgetInfo(
    const DashboardRenderer& renderer, const LayoutNodeConfig& node) const {
    if (DashboardRenderer::IsContainerNode(node)) {
        return nullptr;
    }

    const auto it = parsedWidgetInfoCache_.find(&node);
    if (it != parsedWidgetInfoCache_.end()) {
        return &it->second;
    }

    if (node.name.empty() || !EnumFromString<WidgetClass>(node.name).has_value()) {
        return nullptr;
    }

    auto widget = CreateWidget(node.name);
    if (widget == nullptr) {
        return nullptr;
    }

    widget->Initialize(node);
    ParsedWidgetInfo info;
    info.preferredHeight = widget->PreferredHeight(renderer);
    info.fixedPreferredHeightInRows = widget->UsesFixedPreferredHeightInRows();
    info.verticalSpring = widget->IsVerticalSpring();
    info.widgetPrototype = std::move(widget);
    return &parsedWidgetInfoCache_.emplace(&node, std::move(info)).first->second;
}

WidgetLayout DashboardLayoutResolver::ResolveWidgetLayout(const DashboardRenderer& renderer,
    const LayoutNodeConfig& node,
    const RenderRect& rect,
    bool instantiateWidget) const {
    WidgetLayout widget;
    widget.rect = rect;
    const ParsedWidgetInfo* info = FindParsedWidgetInfo(renderer, node);
    if (instantiateWidget && info != nullptr) {
        widget.widget = info->widgetPrototype->Clone();
    }
    return widget;
}

void DashboardLayoutResolver::ResolveNodeWidgetsInternal(DashboardRenderer& renderer,
    const LayoutNodeConfig& node,
    const RenderRect& rect,
    std::vector<WidgetLayout>& widgets,
    std::vector<std::string>& cardReferenceStack,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath,
    bool instantiateWidgets) {
    renderer.WriteTrace("renderer:layout_resolve_node name=\"" + node.name +
                        "\" weight=" + std::to_string(node.weight) + " " + FormatRect(rect) +
                        " children=" + std::to_string(node.children.size()));
    if (node.cardReference) {
        if (ContainsCardReference(cardReferenceStack, node.name)) {
            renderer.WriteTrace("renderer:layout_card_ref_cycle id=\"" + node.name + "\"");
            return;
        }
        const LayoutCardConfig* referencedCard = renderer.FindCardConfigById(node.name);
        if (referencedCard == nullptr) {
            renderer.WriteTrace("renderer:layout_card_ref_missing id=\"" + node.name + "\"");
            return;
        }
        renderer.WriteTrace("renderer:layout_card_ref id=\"" + node.name + "\" " + FormatRect(rect));
        cardReferenceStack.push_back(node.name);
        ResolveNodeWidgetsInternal(renderer,
            referencedCard->layout,
            rect,
            widgets,
            cardReferenceStack,
            renderCardId,
            node.name,
            {},
            instantiateWidgets);
        cardReferenceStack.pop_back();
        return;
    }
    if (!DashboardRenderer::IsContainerNode(node)) {
        WidgetLayout widget = ResolveWidgetLayout(renderer, node, rect, instantiateWidgets);
        widget.cardId = renderCardId;
        widget.editCardId = editCardId;
        widget.nodePath = nodePath;
        const std::string widgetTypeName =
            widget.widget != nullptr ? std::string(EnumToString(widget.widget->Class())) : std::string();
        renderer.WriteTrace("renderer:layout_widget_resolved kind=\"" + node.name + "\" " + FormatRect(widget.rect) +
                            (widgetTypeName.empty() ? "" : " type=\"" + widgetTypeName + "\""));
        widgets.push_back(std::move(widget));
        return;
    }

    const bool horizontal = node.name == "columns";
    const int gap = horizontal ? renderer.ScaleLogical(renderer.config_.layout.cardStyle.columnGap)
                               : renderer.ScaleLogical(renderer.config_.layout.cardStyle.rowGap);

    const int totalAvailable = (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
                               gap * static_cast<int>((std::max)(static_cast<size_t>(0), node.children.size() - 1));
    int reservedPreferred = 0;
    int totalWeight = 0;
    int springWeight = 0;
    const bool rowsUseSprings =
        !horizontal && std::any_of(node.children.begin(), node.children.end(), [&](const auto& child) {
            const DashboardLayoutResolver::ParsedWidgetInfo* childWidget = FindParsedWidgetInfo(renderer, child);
            return childWidget != nullptr && childWidget->verticalSpring;
        });
    if (!horizontal) {
        for (const auto& child : node.children) {
            const DashboardLayoutResolver::ParsedWidgetInfo* childWidget = FindParsedWidgetInfo(renderer, child);
            if (childWidget != nullptr && childWidget->verticalSpring) {
                springWeight += (std::max)(1, child.weight);
                continue;
            }
            if (childWidget != nullptr && childWidget->fixedPreferredHeightInRows) {
                reservedPreferred += (std::max)(0, childWidget->preferredHeight);
            } else if (rowsUseSprings) {
                reservedPreferred += (std::max)(0, PreferredNodeHeight(renderer, child, rect.right - rect.left));
            } else {
                totalWeight += (std::max)(1, child.weight);
            }
        }
    } else {
        for (const auto& child : node.children) {
            totalWeight += (std::max)(1, child.weight);
        }
    }
    const int distributableAvailable = horizontal ? totalAvailable : (std::max)(0, totalAvailable - reservedPreferred);
    if (horizontal && totalWeight <= 0) {
        return;
    }

    int remainingDistributable = distributableAvailable;
    int cursor = horizontal ? rect.left : rect.top;
    std::vector<RenderRect> childRects;
    childRects.reserve(node.children.size());
    for (size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = node.children[i];
        const DashboardLayoutResolver::ParsedWidgetInfo* childWidget = FindParsedWidgetInfo(renderer, child);
        const bool fixedPreferred = !horizontal && childWidget != nullptr && childWidget->fixedPreferredHeightInRows;
        const bool verticalSpring = !horizontal && childWidget != nullptr && childWidget->verticalSpring;
        const bool preferredPacked = !horizontal && rowsUseSprings && !verticalSpring;
        const int childWeight = (fixedPreferred || preferredPacked) ? 0 : (std::max)(1, child.weight);
        const int remainingWeight = (std::max)(1, totalWeight);
        int size = 0;
        if (fixedPreferred) {
            size = (std::max)(0, childWidget->preferredHeight);
        } else if (preferredPacked) {
            size = (std::max)(0, PreferredNodeHeight(renderer, child, rect.right - rect.left));
        } else if (verticalSpring) {
            if (i + 1 == node.children.size()) {
                size = remainingDistributable;
            } else {
                size =
                    (std::max)(0, remainingDistributable * (std::max)(1, child.weight) / (std::max)(1, springWeight));
            }
        } else if (i + 1 == node.children.size()) {
            size = (horizontal ? rect.right : rect.bottom) - cursor;
        } else {
            size = (std::max)(0, remainingDistributable * childWeight / remainingWeight);
        }
        const int remainingExtent = (std::max)(0, (horizontal ? rect.right : rect.bottom) - cursor);
        size = (std::min)((std::max)(0, size), remainingExtent);

        RenderRect childRect = rect;
        if (horizontal) {
            childRect.left = cursor;
            childRect.right = cursor + size;
        } else {
            childRect.top = cursor;
            childRect.bottom = cursor + size;
        }

        renderer.WriteTrace("renderer:layout_weighted_child parent=\"" + node.name + "\" child=\"" + child.name +
                            "\" weight=" + std::to_string(childWeight) + " gap=" + std::to_string(gap) +
                            " size=" + std::to_string(size) + " " + FormatRect(childRect));
        childRects.push_back(childRect);
        std::vector<size_t> childPath = nodePath;
        childPath.push_back(i);
        ResolveNodeWidgetsInternal(renderer,
            child,
            childRect,
            widgets,
            cardReferenceStack,
            renderCardId,
            editCardId,
            childPath,
            instantiateWidgets);
        cursor += size + gap;
        if (verticalSpring) {
            remainingDistributable -= size;
            springWeight -= (std::max)(1, child.weight);
        } else if (!fixedPreferred && !preferredPacked) {
            remainingDistributable -= size;
            totalWeight -= childWeight;
        }
    }
    if (instantiateWidgets) {
        AddLayoutEditGuide(renderer, node, rect, childRects, gap, renderCardId, editCardId, nodePath);
    }
}

bool DashboardLayoutResolver::ResolveLayout(DashboardRenderer& renderer, bool includeWidgetState) {
    resolvedLayout_ = {};
    layoutEditGuides_.clear();
    containerChildReorderTargets_.clear();
    widgetEditGuides_.clear();
    gapEditAnchors_.clear();
    staticEditableAnchorRegions_.clear();
    dynamicEditableAnchorRegions_.clear();
    staticColorEditRegions_.clear();
    dynamicColorEditRegions_.clear();
    parsedWidgetInfoCache_.clear();
    resolvedLayout_.windowWidth = renderer.WindowWidth();
    resolvedLayout_.windowHeight = renderer.WindowHeight();

    const RenderRect dashboardRect{renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
        renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
        renderer.WindowWidth() - renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin),
        renderer.WindowHeight() - renderer.ScaleLogical(renderer.config_.layout.dashboard.outerMargin)};

    if (renderer.config_.layout.structure.cardsLayout.name.empty()) {
        renderer.lastError_ = "renderer:layout_missing_cards_root";
        return false;
    }

    renderer.WriteTrace("renderer:layout_begin window=" + std::to_string(resolvedLayout_.windowWidth) + "x" +
                        std::to_string(resolvedLayout_.windowHeight) + " " + FormatRect(dashboardRect) +
                        " cards_root=\"" + renderer.config_.layout.structure.cardsLayout.name + "\"");

    if (includeWidgetState && !renderer.layoutGuideDragActive_) {
        LayoutEditGapAnchor anchor;
        anchor.axis = LayoutGuideAxis::Horizontal;
        anchor.key.widget = LayoutEditWidgetIdentity{"", "", {}, LayoutEditWidgetIdentity::Kind::DashboardChrome};
        anchor.key.parameter = DashboardRenderer::LayoutEditParameter::DashboardOuterMargin;
        const int handleSize = (std::max)(4, renderer.ScaleLogical(6));
        const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
        anchor.drawStart = RenderPoint{0, dashboardRect.top};
        anchor.drawEnd = RenderPoint{dashboardRect.left, dashboardRect.top};
        anchor.handleRect = MakeSquareAnchorRect(anchor.drawEnd.x, anchor.drawEnd.y, handleSize);
        anchor.hitRect = anchor.handleRect.Inflate(hitInset, hitInset);
        anchor.dragAxis = AnchorDragAxis::Horizontal;
        anchor.value = renderer.Config().layout.dashboard.outerMargin;
        gapEditAnchors_.push_back(std::move(anchor));
    }

    const auto resolveCard =
        [&](const LayoutNodeConfig& node, const RenderRect& rect, const std::vector<size_t>& nodePath) {
            const auto cardIt = std::find_if(renderer.config_.layout.cards.begin(),
                renderer.config_.layout.cards.end(),
                [&](const auto& card) { return card.id == node.name; });
            if (cardIt == renderer.config_.layout.cards.end()) {
                return;
            }

            DashboardLayoutResolver::ResolvedCardLayout card;
            card.id = cardIt->id;
            card.title = cardIt->title;
            card.iconName = cardIt->icon;
            card.nodePath = nodePath;
            card.rect = rect;
            card.chromeLayout = ResolveCardChromeLayout(*cardIt, card.rect, ResolveCardChromeLayoutMetrics(renderer));
            card.chrome.rect = card.rect;
            card.chrome.cardId = card.id;
            card.chrome.editCardId = card.id;
            card.chrome.widget = includeWidgetState ? CreateCardChromeWidget(*cardIt) : nullptr;

            renderer.WriteTrace("renderer:layout_card id=\"" + card.id + "\" " + FormatRect(card.rect) +
                                " title=" + FormatRect(card.chromeLayout.titleRect) +
                                " icon=" + FormatRect(card.chromeLayout.iconRect) +
                                " content=" + FormatRect(card.chromeLayout.contentRect));
            std::vector<std::string> cardReferenceStack;
            ResolveNodeWidgetsInternal(renderer,
                cardIt->layout,
                card.chromeLayout.contentRect,
                card.widgets,
                cardReferenceStack,
                card.id,
                card.id,
                {},
                includeWidgetState);
            resolvedLayout_.cards.push_back(std::move(card));
        };

    std::function<void(const LayoutNodeConfig&, const RenderRect&, const std::vector<size_t>&)> resolveDashboardNode =
        [&](const LayoutNodeConfig& node, const RenderRect& rect, const std::vector<size_t>& nodePath) {
            if (!DashboardRenderer::IsContainerNode(node)) {
                resolveCard(node, rect, nodePath);
                return;
            }

            const bool horizontal = node.name == "columns";
            const int gap = horizontal ? renderer.ScaleLogical(renderer.config_.layout.dashboard.columnGap)
                                       : renderer.ScaleLogical(renderer.config_.layout.dashboard.rowGap);
            int totalWeight = 0;
            for (const auto& child : node.children) {
                totalWeight += (std::max)(1, child.weight);
            }
            if (totalWeight <= 0) {
                return;
            }

            const int totalAvailable =
                (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
                gap * static_cast<int>((std::max)(static_cast<size_t>(0), node.children.size() - 1));
            int remainingAvailable = totalAvailable;
            int cursor = horizontal ? rect.left : rect.top;
            int remainingWeight = totalWeight;
            std::vector<RenderRect> childRects;
            childRects.reserve(node.children.size());
            for (size_t i = 0; i < node.children.size(); ++i) {
                const auto& child = node.children[i];
                const int childWeight = (std::max)(1, child.weight);
                const int size = (i + 1 == node.children.size())
                                     ? ((horizontal ? rect.right : rect.bottom) - cursor)
                                     : (std::max)(0, remainingAvailable * childWeight / (std::max)(1, remainingWeight));

                RenderRect childRect = rect;
                if (horizontal) {
                    childRect.left = cursor;
                    childRect.right = cursor + size;
                } else {
                    childRect.top = cursor;
                    childRect.bottom = cursor + size;
                }

                renderer.WriteTrace("renderer:layout_dashboard_child parent=\"" + node.name + "\" child=\"" +
                                    child.name + "\" weight=" + std::to_string(childWeight) +
                                    " gap=" + std::to_string(gap) + " size=" + std::to_string(size) + " " +
                                    FormatRect(childRect));
                childRects.push_back(childRect);
                std::vector<size_t> childPath = nodePath;
                childPath.push_back(i);
                resolveDashboardNode(child, childRect, childPath);
                cursor += size + gap;
                remainingAvailable -= size;
                remainingWeight -= childWeight;
            }
            if (includeWidgetState) {
                AddLayoutEditGuide(renderer, node, rect, childRects, gap, "", "", nodePath);
            }
        };

    resolveDashboardNode(renderer.config_.layout.structure.cardsLayout, dashboardRect, {});

    if (resolvedLayout_.cards.empty()) {
        renderer.lastError_ = "renderer:layout_resolve_failed cards=0 root=\"" +
                              renderer.config_.layout.structure.cardsLayout.name + "\"";
        return false;
    }

    if (includeWidgetState) {
        std::map<WidgetClass, std::vector<WidgetLayout*>> widgetGroups;
        for (auto& card : resolvedLayout_.cards) {
            for (auto& widget : card.widgets) {
                if (widget.widget == nullptr) {
                    continue;
                }
                widgetGroups[widget.widget->Class()].push_back(&widget);
            }
        }
        for (auto& [_, group] : widgetGroups) {
            if (!group.empty() && group.front()->widget != nullptr) {
                group.front()->widget->FinalizeLayoutGroup(renderer, group);
            }
        }
        for (auto& card : resolvedLayout_.cards) {
            if (card.chrome.widget != nullptr) {
                card.chrome.widget->ResolveLayoutState(renderer, card.chrome.rect);
            }
            for (auto& widget : card.widgets) {
                if (widget.widget != nullptr) {
                    widget.widget->ResolveLayoutState(renderer, widget.rect);
                }
            }
        }

        if (renderer.layoutGuideDragActive_) {
            widgetEditGuides_.clear();
            staticEditableAnchorRegions_.clear();
            staticColorEditRegions_.clear();
        } else {
            BuildWidgetEditGuides(renderer);
            BuildStaticEditableAnchors(renderer);
        }
    }

    renderer.WriteTrace("renderer:layout_done cards=" + std::to_string(resolvedLayout_.cards.size()));
    return true;
}
