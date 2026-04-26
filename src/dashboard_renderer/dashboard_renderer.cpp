#include "dashboard_renderer/dashboard_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dashboard_renderer/impl/layout_edit_overlay_renderer.h"
#include "dashboard_renderer/impl/layout_resolver.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_hit_priority.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "layout_model/layout_edit_service.h"
#include "util/strings.h"
#include "util/trace.h"

namespace {

std::string FormatTraceRect(const RenderRect& rect) {
    return "(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," + std::to_string(rect.right) + "," +
           std::to_string(rect.bottom) + ")";
}

std::string FormatNodePath(const std::vector<size_t>& nodePath) {
    if (nodePath.empty()) {
        return "root";
    }
    std::string text;
    for (size_t index : nodePath) {
        if (!text.empty()) {
            text += "/";
        }
        text += "children[";
        text += std::to_string(index);
        text += "]";
    }
    return text;
}

std::string ActiveLayoutSectionName(const AppConfig& config) {
    return config.display.layout.empty() ? "layout" : "layout." + config.display.layout;
}

std::string FormatLayoutConfigPath(
    const AppConfig& config, const std::string& editCardId, const std::vector<size_t>& nodePath) {
    if (editCardId.empty()) {
        return ActiveLayoutSectionName(config) + ".cards/" + FormatNodePath(nodePath);
    }
    return "card." + editCardId + ".layout/" + FormatNodePath(nodePath);
}

std::string FormatLayoutEditParameterPath(LayoutEditParameter parameter) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
    if (!descriptor.has_value()) {
        return "parameter";
    }
    return descriptor->configKey;
}

std::string FormatLayoutEditParameterDetail(LayoutEditParameter parameter) {
    return GetLayoutEditParameterDisplayName(parameter) + " (" + FormatLayoutEditParameterPath(parameter) + ")";
}

std::string FormatWidgetIdentityPath(const AppConfig& config, const LayoutEditWidgetIdentity& widget) {
    switch (widget.kind) {
        case LayoutEditWidgetIdentity::Kind::DashboardChrome:
            return ActiveLayoutSectionName(config) + ".dashboard";
        case LayoutEditWidgetIdentity::Kind::CardChrome:
            return ActiveLayoutSectionName(config) + ".cards/card[" + widget.editCardId + "]";
        case LayoutEditWidgetIdentity::Kind::Widget:
        default:
            return FormatLayoutConfigPath(config, widget.editCardId, widget.nodePath);
    }
}

std::string FormatGuideAxis(LayoutGuideAxis axis) {
    return axis == LayoutGuideAxis::Vertical ? "vertical" : "horizontal";
}

std::string FormatAnchorShape(AnchorShape shape) {
    switch (shape) {
        case AnchorShape::Circle:
            return "circle";
        case AnchorShape::Diamond:
            return "diamond";
        case AnchorShape::Square:
            return "square";
        case AnchorShape::Wedge:
            return "wedge";
        case AnchorShape::VerticalReorder:
            return "vertical-reorder";
        case AnchorShape::HorizontalReorder:
            return "horizontal-reorder";
        case AnchorShape::Plus:
            return "plus";
    }
    return "unknown";
}

std::string FormatAnchorSubject(const AppConfig& config, const LayoutEditAnchorKey& key) {
    if (const auto parameter = LayoutEditAnchorParameter(key); parameter.has_value()) {
        return FormatLayoutEditParameterDetail(*parameter);
    }
    if (const auto metric = LayoutEditAnchorMetricKey(key); metric.has_value()) {
        return "metric binding " + metric->metricId;
    }
    if (const auto title = LayoutEditAnchorCardTitleKey(key); title.has_value()) {
        return "card title " + title->cardId;
    }
    if (const auto order = LayoutEditAnchorMetricListOrderKey(key); order.has_value()) {
        return "metric list order " + FormatLayoutConfigPath(config, order->editCardId, order->nodePath);
    }
    if (const auto order = LayoutEditAnchorContainerChildOrderKey(key); order.has_value()) {
        return "container child order " + FormatLayoutConfigPath(config, order->editCardId, order->nodePath);
    }
    return "unknown anchor subject";
}

std::string FormatActiveRegionPhase(DashboardActiveRegionKind kind) {
    switch (kind) {
        case DashboardActiveRegionKind::DynamicEditAnchorHandle:
        case DashboardActiveRegionKind::DynamicEditAnchorTarget:
        case DashboardActiveRegionKind::DynamicColorTarget:
            return "dynamic";
        default:
            return "static";
    }
}

bool IsActiveRegionAnchorHandle(DashboardActiveRegionKind kind) {
    return kind == DashboardActiveRegionKind::StaticEditAnchorHandle ||
           kind == DashboardActiveRegionKind::DynamicEditAnchorHandle;
}

std::string FormatActiveRegionVisualType(DashboardActiveRegionKind kind) {
    switch (kind) {
        case DashboardActiveRegionKind::Card:
            return "card";
        case DashboardActiveRegionKind::CardHeader:
            return "card-header";
        case DashboardActiveRegionKind::WidgetHover:
            return "widget-hover";
        case DashboardActiveRegionKind::LayoutWeightGuide:
            return "layout-weight-guide";
        case DashboardActiveRegionKind::ContainerChildReorderTarget:
            return "container-child-reorder-target";
        case DashboardActiveRegionKind::GapHandle:
            return "gap-handle";
        case DashboardActiveRegionKind::WidgetGuide:
            return "widget-guide";
        case DashboardActiveRegionKind::StaticEditAnchorHandle:
        case DashboardActiveRegionKind::DynamicEditAnchorHandle:
            return "edit-anchor-handle";
        case DashboardActiveRegionKind::StaticEditAnchorTarget:
        case DashboardActiveRegionKind::DynamicEditAnchorTarget:
            return "edit-anchor-target";
        case DashboardActiveRegionKind::StaticColorTarget:
        case DashboardActiveRegionKind::DynamicColorTarget:
            return "color-target";
    }
    return "unknown";
}

std::string FormatActiveRegionPath(const AppConfig& config, const DashboardActiveRegion& activeRegion) {
    switch (activeRegion.kind) {
        case DashboardActiveRegionKind::Card: {
            const auto& card = *std::get<const DashboardLayoutResolver::ResolvedCardLayout*>(activeRegion.payload);
            return ActiveLayoutSectionName(config) + ".cards/" + FormatNodePath(card.nodePath) + "/card[" + card.id +
                   "]";
        }
        case DashboardActiveRegionKind::CardHeader: {
            const auto& card = *std::get<const DashboardLayoutResolver::ResolvedCardLayout*>(activeRegion.payload);
            return ActiveLayoutSectionName(config) + ".cards/" + FormatNodePath(card.nodePath) + "/card[" + card.id +
                   "]/header";
        }
        case DashboardActiveRegionKind::WidgetHover: {
            const auto& widget = *std::get<const WidgetLayout*>(activeRegion.payload);
            const WidgetClass widgetClass = widget.widget != nullptr ? widget.widget->Class() : WidgetClass::Unknown;
            return FormatLayoutConfigPath(config, widget.editCardId, widget.nodePath) + "/widget[" +
                   std::string(EnumToString(widgetClass)) + "]";
        }
        case DashboardActiveRegionKind::LayoutWeightGuide: {
            const auto& guide = *std::get<const LayoutEditGuide*>(activeRegion.payload);
            return FormatLayoutConfigPath(config, guide.editCardId, guide.nodePath) + "/separator[" +
                   std::to_string(guide.separatorIndex) + "]";
        }
        case DashboardActiveRegionKind::ContainerChildReorderTarget: {
            const auto& target =
                *std::get<const DashboardLayoutResolver::ContainerChildReorderTarget*>(activeRegion.payload);
            return FormatLayoutConfigPath(config, target.editCardId, target.nodePath) + "/child-reorder-target";
        }
        case DashboardActiveRegionKind::GapHandle: {
            const auto& anchor = *std::get<const LayoutEditGapAnchor*>(activeRegion.payload);
            return FormatWidgetIdentityPath(config, anchor.key.widget) + "/gap/" +
                   FormatLayoutConfigPath(config, anchor.key.widget.editCardId, anchor.key.nodePath);
        }
        case DashboardActiveRegionKind::WidgetGuide: {
            const auto& guide = *std::get<const LayoutEditWidgetGuide*>(activeRegion.payload);
            return FormatWidgetIdentityPath(config, guide.widget) + "/guide[" + std::to_string(guide.guideId) + "]";
        }
        case DashboardActiveRegionKind::StaticEditAnchorHandle:
        case DashboardActiveRegionKind::StaticEditAnchorTarget:
        case DashboardActiveRegionKind::DynamicEditAnchorHandle:
        case DashboardActiveRegionKind::DynamicEditAnchorTarget: {
            const auto& region = *std::get<const LayoutEditAnchorRegion*>(activeRegion.payload);
            const std::string suffix = IsActiveRegionAnchorHandle(activeRegion.kind) ? "/handle" : "/target";
            return FormatWidgetIdentityPath(config, region.key.widget) + "/anchor[" +
                   std::to_string(region.key.anchorId) + "]" + suffix;
        }
        case DashboardActiveRegionKind::StaticColorTarget:
        case DashboardActiveRegionKind::DynamicColorTarget: {
            const auto& region = *std::get<const LayoutEditColorRegion*>(activeRegion.payload);
            return FormatLayoutEditParameterPath(region.parameter);
        }
    }
    return {};
}

std::string FormatActiveRegionDetail(const AppConfig& config, const DashboardActiveRegion& activeRegion) {
    switch (activeRegion.kind) {
        case DashboardActiveRegionKind::Card: {
            const auto& card = *std::get<const DashboardLayoutResolver::ResolvedCardLayout*>(activeRegion.payload);
            return "card chrome " + card.id;
        }
        case DashboardActiveRegionKind::CardHeader: {
            const auto& card = *std::get<const DashboardLayoutResolver::ResolvedCardLayout*>(activeRegion.payload);
            return "card header " + card.id;
        }
        case DashboardActiveRegionKind::WidgetHover: {
            const auto& widget = *std::get<const WidgetLayout*>(activeRegion.payload);
            const WidgetClass widgetClass = widget.widget != nullptr ? widget.widget->Class() : WidgetClass::Unknown;
            return "hoverable widget " + std::string(EnumToString(widgetClass)) + " in card " + widget.cardId;
        }
        case DashboardActiveRegionKind::LayoutWeightGuide: {
            const auto& guide = *std::get<const LayoutEditGuide*>(activeRegion.payload);
            return FormatGuideAxis(guide.axis) + " layout weight separator";
        }
        case DashboardActiveRegionKind::ContainerChildReorderTarget: {
            const auto& target =
                *std::get<const DashboardLayoutResolver::ContainerChildReorderTarget*>(activeRegion.payload);
            return FormatGuideAxis(target.horizontal ? LayoutGuideAxis::Horizontal : LayoutGuideAxis::Vertical) +
                   " container child reorder target";
        }
        case DashboardActiveRegionKind::GapHandle: {
            const auto& anchor = *std::get<const LayoutEditGapAnchor*>(activeRegion.payload);
            return FormatLayoutEditParameterDetail(anchor.key.parameter);
        }
        case DashboardActiveRegionKind::WidgetGuide: {
            const auto& guide = *std::get<const LayoutEditWidgetGuide*>(activeRegion.payload);
            return FormatGuideAxis(guide.axis) + " " + FormatLayoutEditParameterDetail(guide.parameter);
        }
        case DashboardActiveRegionKind::StaticEditAnchorHandle:
        case DashboardActiveRegionKind::StaticEditAnchorTarget:
        case DashboardActiveRegionKind::DynamicEditAnchorHandle:
        case DashboardActiveRegionKind::DynamicEditAnchorTarget: {
            const auto& region = *std::get<const LayoutEditAnchorRegion*>(activeRegion.payload);
            return FormatActiveRegionPhase(activeRegion.kind) + " " + FormatAnchorShape(region.shape) + " " +
                   FormatAnchorSubject(config, region.key);
        }
        case DashboardActiveRegionKind::StaticColorTarget:
        case DashboardActiveRegionKind::DynamicColorTarget: {
            const auto& region = *std::get<const LayoutEditColorRegion*>(activeRegion.payload);
            return FormatActiveRegionPhase(activeRegion.kind) + " color " +
                   FormatLayoutEditParameterDetail(region.parameter);
        }
    }
    return {};
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

bool SameRect(const RenderRect& left, const RenderRect& right) {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

RenderRect OffsetRect(RenderRect rect, int dx, int dy) {
    rect.left += dx;
    rect.right += dx;
    rect.top += dy;
    rect.bottom += dy;
    return rect;
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

DashboardRenderer::DashboardRenderer(Trace& trace)
    : trace_(trace), renderer_(CreateRenderer()), layoutResolver_(std::make_unique<DashboardLayoutResolver>()),
      layoutEditOverlayRenderer_(std::make_unique<DashboardLayoutEditOverlayRenderer>(*this, *layoutResolver_)) {}

DashboardRenderer::~DashboardRenderer() {
    Shutdown();
}

void DashboardRenderer::SetConfig(const AppConfig& config) {
    lastError_.clear();
    const bool metricsChanged = config_.layout.metrics != config.layout.metrics;
    if (metricsChanged) {
        InvalidateMetricSourceCache();
        metricDefinitionCache_.clear();
        metricSampleValueTextCache_.clear();
    }
    config_ = config;
    if (!renderer_->SetStyle(BuildRendererStyle()) || !ResolveLayout()) {
        lastError_ = renderer_->LastError().empty() ? "renderer:reconfigure_failed" : renderer_->LastError();
    }
}

void DashboardRenderer::SetRenderScale(double scale) {
    lastError_.clear();
    const double nextScale = std::clamp(scale, 0.1, 16.0);
    if (std::abs(renderScale_ - nextScale) < 0.0001) {
        return;
    }
    renderScale_ = nextScale;
    if (!renderer_->SetStyle(BuildRendererStyle()) || !ResolveLayout()) {
        lastError_ = renderer_->LastError().empty() ? "renderer:rescale_failed" : renderer_->LastError();
    }
}

void DashboardRenderer::SetImmediatePresent(bool enabled) {
    renderer_->SetImmediatePresent(enabled);
}

void DashboardRenderer::SetRenderMode(RenderMode mode) {
    renderMode_ = mode;
}

void DashboardRenderer::SetLayoutGuideDragActive(bool active) {
    layoutGuideDragActive_ = active;
}

void DashboardRenderer::SetInteractiveDragTraceActive(bool active) {
    interactiveDragTraceActive_ = active;
}

void DashboardRenderer::RebuildEditArtifacts() {
    BuildWidgetEditGuides();
    BuildStaticEditableAnchors();
}

double DashboardRenderer::RenderScale() const {
    return renderScale_;
}

const std::string& DashboardRenderer::LastError() const {
    return lastError_.empty() ? renderer_->LastError() : lastError_;
}

::Renderer& DashboardRenderer::Renderer() {
    return *renderer_;
}

const ::Renderer& DashboardRenderer::Renderer() const {
    return *renderer_;
}

const AppConfig& DashboardRenderer::Config() const {
    return config_;
}

DashboardRenderer::RenderMode DashboardRenderer::CurrentRenderMode() const {
    return renderMode_;
}

std::vector<LayoutEditWidgetGuide>& DashboardRenderer::WidgetEditGuidesMutable() {
    return layoutResolver_->widgetEditGuides_;
}

std::vector<LayoutEditGapAnchor>& DashboardRenderer::GapEditAnchorsMutable() {
    return layoutResolver_->gapEditAnchors_;
}

std::optional<MetricListReorderOverlayState> DashboardRenderer::ActiveMetricListReorderDrag(
    const LayoutEditWidgetIdentity& widget) const {
    if (activeOverlayState_ == nullptr || !activeOverlayState_->activeMetricListReorderDrag.has_value()) {
        return std::nullopt;
    }

    const MetricListReorderOverlayState& drag = *activeOverlayState_->activeMetricListReorderDrag;
    if (drag.widget.kind != widget.kind || drag.widget.renderCardId != widget.renderCardId ||
        drag.widget.editCardId != widget.editCardId || drag.widget.nodePath != widget.nodePath) {
        return std::nullopt;
    }
    return drag;
}

int DashboardRenderer::WindowWidth() const {
    return std::max(1, ScaleLogical(config_.layout.structure.window.width));
}

int DashboardRenderer::WindowHeight() const {
    return std::max(1, ScaleLogical(config_.layout.structure.window.height));
}

const std::vector<LayoutEditGuide>& DashboardRenderer::LayoutEditGuides() const {
    return layoutResolver_->layoutEditGuides_;
}

const std::vector<LayoutEditWidgetGuide>& DashboardRenderer::WidgetEditGuides() const {
    return layoutResolver_->widgetEditGuides_;
}

const std::vector<LayoutEditGapAnchor>& DashboardRenderer::GapEditAnchors() const {
    return layoutResolver_->gapEditAnchors_;
}

int DashboardRenderer::LayoutSimilarityThreshold() const {
    return std::max(0, ScaleLogical(config_.layout.layoutEditor.sizeSimilarityThreshold));
}

void DashboardRenderer::ResolveNodeWidgets(
    const LayoutNodeConfig& node, const RenderRect& rect, std::vector<WidgetLayout>& widgets, bool instantiateWidgets) {
    layoutResolver_->ResolveNodeWidgets(*this, node, rect, widgets, instantiateWidgets);
}

void DashboardRenderer::BuildWidgetEditGuides() {
    layoutResolver_->BuildWidgetEditGuides(*this);
}

void DashboardRenderer::BuildStaticEditableAnchors() {
    layoutResolver_->BuildStaticEditableAnchors(*this);
}

void DashboardRenderer::AddLayoutEditGuide(const LayoutNodeConfig& node,
    const RenderRect& rect,
    const std::vector<RenderRect>& childRects,
    int gap,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath) {
    layoutResolver_->AddLayoutEditGuide(*this, node, rect, childRects, gap, renderCardId, editCardId, nodePath);
}

void DashboardRenderer::ResolveNodeWidgetsInternal(const LayoutNodeConfig& node,
    const RenderRect& rect,
    std::vector<WidgetLayout>& widgets,
    std::vector<std::string>& cardReferenceStack,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath,
    bool instantiateWidgets) {
    layoutResolver_->ResolveNodeWidgetsInternal(
        *this, node, rect, widgets, cardReferenceStack, renderCardId, editCardId, nodePath, instantiateWidgets);
}

bool DashboardRenderer::ResolveLayout(bool includeWidgetState) {
    return layoutResolver_->ResolveLayout(*this, includeWidgetState);
}

LayoutEditAnchorBinding DashboardRenderer::MakeEditableTextBinding(
    const WidgetLayout& widget, LayoutEditParameter parameter, int anchorId, int value) const {
    LayoutEditAnchorBinding binding;
    binding.key.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    binding.key.subject = parameter;
    binding.key.anchorId = anchorId;
    binding.value = value;
    binding.shape = AnchorShape::Circle;
    binding.dragAxis = AnchorDragAxis::Vertical;
    binding.dragMode = AnchorDragMode::AxisDelta;
    binding.draggable = true;
    return binding;
}

LayoutEditAnchorBinding DashboardRenderer::MakeMetricTextBinding(
    const WidgetLayout& widget, std::string_view metricId, int anchorId) const {
    LayoutEditAnchorBinding binding;
    binding.key.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    binding.key.subject = LayoutMetricEditKey{std::string(metricId)};
    binding.key.anchorId = anchorId;
    binding.value = 0;
    binding.shape = AnchorShape::Wedge;
    binding.dragAxis = AnchorDragAxis::Vertical;
    binding.dragMode = AnchorDragMode::AxisDelta;
    binding.draggable = false;
    return binding;
}

void DashboardRenderer::RegisterEditableAnchorRegion(std::vector<LayoutEditAnchorRegion>& regions,
    const LayoutEditAnchorKey& key,
    const RenderRect& targetRect,
    const RenderRect& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    RenderPoint dragOrigin,
    double dragScale,
    bool draggable,
    bool showWhenWidgetHovered,
    bool drawTargetOutline,
    int value) {
    if (anchorRect.right <= anchorRect.left || anchorRect.bottom <= anchorRect.top) {
        return;
    }
    LayoutEditAnchorRegion region;
    region.key = key;
    region.targetRect = targetRect;
    region.anchorRect = anchorRect;
    region.shape = shape;
    const int anchorHitInset =
        shape == AnchorShape::Wedge ? std::max(1, ScaleLogical(2)) : std::max(3, ScaleLogical(4));
    region.anchorHitPadding = anchorHitInset;
    region.anchorHitRect = RenderRect{region.anchorRect.left - anchorHitInset,
        region.anchorRect.top - anchorHitInset,
        region.anchorRect.right + anchorHitInset,
        region.anchorRect.bottom + anchorHitInset};
    region.dragAxis = dragAxis;
    region.dragMode = dragMode;
    region.dragOrigin = dragOrigin;
    region.dragScale = dragScale;
    region.draggable = draggable;
    region.showWhenWidgetHovered = showWhenWidgetHovered;
    region.drawTargetOutline = drawTargetOutline;
    region.value = value;
    regions.push_back(std::move(region));
}

void DashboardRenderer::RegisterStaticEditableAnchorRegion(const LayoutEditAnchorKey& key,
    const RenderRect& targetRect,
    const RenderRect& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    RenderPoint dragOrigin,
    double dragScale,
    bool draggable,
    bool showWhenWidgetHovered,
    bool drawTargetOutline,
    int value) {
    RegisterEditableAnchorRegion(layoutResolver_->staticEditableAnchorRegions_,
        key,
        targetRect,
        anchorRect,
        shape,
        dragAxis,
        dragMode,
        dragOrigin,
        dragScale,
        draggable,
        showWhenWidgetHovered,
        drawTargetOutline,
        value);
}

void DashboardRenderer::RegisterDynamicEditableAnchorRegion(const LayoutEditAnchorKey& key,
    const RenderRect& targetRect,
    const RenderRect& anchorRect,
    AnchorShape shape,
    AnchorDragAxis dragAxis,
    AnchorDragMode dragMode,
    RenderPoint dragOrigin,
    double dragScale,
    bool draggable,
    bool showWhenWidgetHovered,
    bool drawTargetOutline,
    int value) {
    if (!layoutResolver_->dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterEditableAnchorRegion(layoutResolver_->dynamicEditableAnchorRegions_,
        key,
        targetRect,
        anchorRect,
        shape,
        dragAxis,
        dragMode,
        dragOrigin,
        dragScale,
        draggable,
        showWhenWidgetHovered,
        drawTargetOutline,
        value);
}

void DashboardRenderer::RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
    const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const LayoutEditAnchorBinding& editable,
    bool drawTargetOutline) {
    if (text.empty()) {
        return;
    }

    const TextLayoutResult result = Renderer().MeasureTextBlock(rect, text, style, options);
    const RenderRect anchorRect = TextAnchorRectForShape(*this, result.textRect, editable.shape);
    const RenderPoint anchorOrigin = anchorRect.Center();
    RegisterEditableAnchorRegion(regions,
        editable.key,
        result.textRect,
        anchorRect,
        editable.shape,
        editable.dragAxis,
        editable.dragMode,
        anchorOrigin,
        1.0,
        editable.draggable,
        false,
        drawTargetOutline,
        editable.value);
}

void DashboardRenderer::RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
    const TextLayoutResult& layoutResult,
    const LayoutEditAnchorBinding& editable,
    bool drawTargetOutline) {
    const RenderRect& textRect = layoutResult.textRect;
    if (textRect.right <= textRect.left || textRect.bottom <= textRect.top) {
        return;
    }

    const RenderRect anchorRect = TextAnchorRectForShape(*this, textRect, editable.shape);
    const RenderPoint anchorOrigin = anchorRect.Center();
    RegisterEditableAnchorRegion(regions,
        editable.key,
        textRect,
        anchorRect,
        editable.shape,
        editable.dragAxis,
        editable.dragMode,
        anchorOrigin,
        1.0,
        editable.draggable,
        false,
        drawTargetOutline,
        editable.value);
}

void DashboardRenderer::RegisterStaticTextAnchor(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const LayoutEditAnchorBinding& editable,
    std::optional<LayoutEditParameter> colorParameter,
    bool drawTargetOutline) {
    RegisterTextAnchor(
        layoutResolver_->staticEditableAnchorRegions_, rect, text, style, options, editable, drawTargetOutline);
    if (colorParameter.has_value()) {
        RegisterStaticColorEditRegion(
            *colorParameter, Renderer().MeasureTextBlock(rect, text, style, options).textRect);
    }
}

void DashboardRenderer::RegisterDynamicTextAnchor(const TextLayoutResult& layoutResult,
    const LayoutEditAnchorBinding& editable,
    std::optional<LayoutEditParameter> colorParameter,
    bool drawTargetOutline) {
    if (!layoutResolver_->dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(layoutResolver_->dynamicEditableAnchorRegions_, layoutResult, editable, drawTargetOutline);
    if (colorParameter.has_value()) {
        RegisterDynamicColorEditRegion(*colorParameter, layoutResult.textRect);
    }
}

void DashboardRenderer::RegisterDynamicTextAnchor(const RenderRect& rect,
    const std::string& text,
    TextStyleId style,
    const TextLayoutOptions& options,
    const LayoutEditAnchorBinding& editable,
    std::optional<LayoutEditParameter> colorParameter,
    bool drawTargetOutline) {
    if (!layoutResolver_->dynamicAnchorRegistrationEnabled_) {
        return;
    }
    RegisterTextAnchor(
        layoutResolver_->dynamicEditableAnchorRegions_, rect, text, style, options, editable, drawTargetOutline);
    if (colorParameter.has_value()) {
        RegisterDynamicColorEditRegion(
            *colorParameter, Renderer().MeasureTextBlock(rect, text, style, options).textRect);
    }
}

void DashboardRenderer::RegisterStaticColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) {
    if (!IsColorEditParameter(parameter) || targetRect.IsEmpty()) {
        return;
    }
    layoutResolver_->staticColorEditRegions_.push_back(LayoutEditColorRegion{parameter, targetRect});
}

void DashboardRenderer::RegisterDynamicColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) {
    if (!layoutResolver_->dynamicAnchorRegistrationEnabled_ || !IsColorEditParameter(parameter) ||
        targetRect.IsEmpty()) {
        return;
    }
    layoutResolver_->dynamicColorEditRegions_.push_back(LayoutEditColorRegion{parameter, targetRect});
}

void DashboardRenderer::DrawMoveOverlay(const DashboardMoveOverlayState& overlayState) {
    if (!overlayState.visible) {
        return;
    }

    const int margin = ScaleLogical(16);
    const int padding = ScaleLogical(12);
    const int lineGap = ScaleLogical(6);
    const int cornerRadius = ScaleLogical(14);
    const float borderWidth = static_cast<float>((std::max)(1, ScaleLogical(1)));
    const int titleHeight = (std::max)(1, Renderer().TextMetrics().label);
    const int bodyHeight = (std::max)(1, Renderer().TextMetrics().smallText);

    char positionTextBuffer[96];
    sprintf_s(positionTextBuffer, "Pos: x=%d y=%d", overlayState.relativePosition.x, overlayState.relativePosition.y);
    char scaleTextBuffer[96];
    sprintf_s(scaleTextBuffer, "Scale: %.0f%% (%.2fx)", overlayState.monitorScale * 100.0, overlayState.monitorScale);

    const std::string titleText = "Move Mode";
    const std::string monitorText = "Monitor: " + overlayState.monitorName;
    const std::string positionText = positionTextBuffer;
    const std::string scaleText = scaleTextBuffer;
    const std::string hintText = "Left-click to place. Copy monitor name, scale, and x/y into config.";

    const int minContentWidth = ScaleLogical(220);
    const int maxContentWidth = (std::max)(minContentWidth, WindowWidth() - margin * 2 - padding * 2);
    int preferredContentWidth = minContentWidth;
    preferredContentWidth =
        (std::max)(preferredContentWidth, Renderer().MeasureTextWidth(TextStyleId::Label, titleText));
    preferredContentWidth =
        (std::max)(preferredContentWidth, Renderer().MeasureTextWidth(TextStyleId::Small, monitorText));
    preferredContentWidth =
        (std::max)(preferredContentWidth, Renderer().MeasureTextWidth(TextStyleId::Small, positionText));
    preferredContentWidth =
        (std::max)(preferredContentWidth, Renderer().MeasureTextWidth(TextStyleId::Small, scaleText));
    const int contentWidth = (std::min)(maxContentWidth, preferredContentWidth);
    const int hintHeight = Renderer()
                               .MeasureTextBlock(RenderRect{0, 0, contentWidth, WindowHeight()},
                                   hintText,
                                   TextStyleId::Small,
                                   TextLayoutOptions::Wrapped())
                               .textRect.Height();
    const int overlayWidth = contentWidth + padding * 2;
    const int overlayHeight = padding * 2 + titleHeight + lineGap + bodyHeight + lineGap + bodyHeight + lineGap +
                              bodyHeight + lineGap + hintHeight;
    const RenderRect overlayRect{margin, margin, margin + overlayWidth, margin + overlayHeight};

    Renderer().FillSolidRoundedRect(overlayRect, cornerRadius, RenderColorId::Background);
    Renderer().DrawSolidRoundedRect(overlayRect, cornerRadius, RenderStroke::Solid(RenderColorId::Accent, borderWidth));

    int y = overlayRect.top + padding;
    Renderer().DrawText(RenderRect{overlayRect.left + padding, y, overlayRect.right - padding, y + titleHeight},
        titleText,
        TextStyleId::Label,
        RenderColorId::Accent,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
    y += titleHeight + lineGap;

    const auto drawBodyLine = [&](const std::string& text, bool ellipsis = false) {
        Renderer().DrawText(RenderRect{overlayRect.left + padding, y, overlayRect.right - padding, y + bodyHeight},
            text,
            TextStyleId::Small,
            RenderColorId::Foreground,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center, true, ellipsis));
        y += bodyHeight + lineGap;
    };
    drawBodyLine(monitorText, true);
    drawBodyLine(positionText);
    drawBodyLine(scaleText);
    Renderer().DrawText(
        RenderRect{overlayRect.left + padding, y, overlayRect.right - padding, overlayRect.bottom - padding},
        hintText,
        TextStyleId::Small,
        RenderColorId::MutedText,
        TextLayoutOptions::Wrapped());
}

void DashboardRenderer::DrawResolvedWidget(const WidgetLayout& widget, const MetricSource& metrics) {
    if (widget.widget == nullptr) {
        return;
    }
    widget.widget->Draw(*this, widget, metrics);
}

bool DashboardRenderer::DrawWindow(const SystemSnapshot& snapshot) {
    return DrawWindow(snapshot, DashboardOverlayState{});
}

bool DashboardRenderer::DrawWindow(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    lastError_.clear();
    renderer_->DrawWindow(WindowWidth(), WindowHeight(), [&] { DrawFrame(snapshot, overlayState); });
    if (!renderer_->LastError().empty()) {
        lastError_ = renderer_->LastError();
    }
    return lastError_.empty();
}

void DashboardRenderer::DrawFrame(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    activeOverlayState_ = &overlayState;
    layoutResolver_->ClearDynamicEditArtifacts();
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = overlayState.ShouldRegisterDynamicEditArtifacts();
    const MetricSource& metrics = ResolveMetrics(snapshot);
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        if (!layoutEditOverlayRenderer_->ShouldSkipBaseWidget(overlayState, card.chrome.rect)) {
            DrawResolvedWidget(card.chrome, metrics);
        }
        for (const auto& widget : card.widgets) {
            if (!layoutEditOverlayRenderer_->ShouldSkipBaseWidget(overlayState, widget.rect)) {
                DrawResolvedWidget(widget, metrics);
            }
        }
    }
    layoutEditOverlayRenderer_->Draw(overlayState, metrics);
    DrawMoveOverlay(overlayState.moveOverlay);
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    activeOverlayState_ = nullptr;
}

bool DashboardRenderer::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    return SaveSnapshotPng(imagePath, snapshot, DashboardOverlayState{});
}

bool DashboardRenderer::SaveSnapshotPng(
    const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    lastError_.clear();
    const bool saved =
        renderer_->SavePng(imagePath, WindowWidth(), WindowHeight(), [&] { DrawFrame(snapshot, overlayState); });
    if (!renderer_->LastError().empty()) {
        lastError_ = renderer_->LastError();
    }
    if (saved) {
        WriteScreenshotActiveRegionsTrace(overlayState);
    }
    return saved;
}

bool DashboardRenderer::RenderSnapshotOffscreen(
    const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    lastError_.clear();
    renderer_->DrawOffscreen(WindowWidth(), WindowHeight(), [&] { DrawFrame(snapshot, overlayState); });
    if (!renderer_->LastError().empty()) {
        lastError_ = renderer_->LastError();
    }
    return lastError_.empty();
}

bool DashboardRenderer::PrimeLayoutEditDynamicRegions(
    const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    return RenderSnapshotOffscreen(snapshot, overlayState);
}

std::vector<DashboardActiveRegion> DashboardRenderer::CollectActiveRegions(
    const DashboardOverlayState& overlayState) const {
    std::vector<DashboardActiveRegion> regions;
    const auto appendRegion =
        [&](const RenderRect& box, DashboardActiveRegionKind kind, DashboardActiveRegionPayload payload) {
            if (box.IsEmpty()) {
                return;
            }
            regions.push_back(DashboardActiveRegion{box, kind, std::move(payload)});
        };

    if (overlayState.showLayoutEditGuides) {
        for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
            appendRegion(card.rect, DashboardActiveRegionKind::Card, &card);
            if (card.chromeLayout.hasHeader) {
                appendRegion(card.chromeLayout.titleRect, DashboardActiveRegionKind::CardHeader, &card);
            }
            for (const auto& widget : card.widgets) {
                if (widget.widget == nullptr || !widget.widget->IsHoverable()) {
                    continue;
                }
                appendRegion(widget.rect, DashboardActiveRegionKind::WidgetHover, &widget);
            }
        }

        for (const auto& guide : layoutResolver_->layoutEditGuides_) {
            appendRegion(guide.hitRect, DashboardActiveRegionKind::LayoutWeightGuide, &guide);
        }

        for (const auto& target : layoutResolver_->containerChildReorderTargets_) {
            for (const RenderRect& childRect : target.childRects) {
                appendRegion(childRect, DashboardActiveRegionKind::ContainerChildReorderTarget, &target);
            }
        }

        for (const auto& anchor : layoutResolver_->gapEditAnchors_) {
            appendRegion(anchor.hitRect, DashboardActiveRegionKind::GapHandle, &anchor);
        }

        for (const auto& guide : layoutResolver_->widgetEditGuides_) {
            appendRegion(guide.hitRect, DashboardActiveRegionKind::WidgetGuide, &guide);
        }

        const auto appendAnchorRegions = [&](const std::vector<LayoutEditAnchorRegion>& regions,
                                             DashboardActiveRegionKind handleKind,
                                             DashboardActiveRegionKind targetKind) {
            for (const auto& region : regions) {
                appendRegion(region.anchorHitRect, handleKind, &region);
                appendRegion(region.targetRect, targetKind, &region);
            }
        };
        appendAnchorRegions(layoutResolver_->staticEditableAnchorRegions_,
            DashboardActiveRegionKind::StaticEditAnchorHandle,
            DashboardActiveRegionKind::StaticEditAnchorTarget);
        appendAnchorRegions(layoutResolver_->dynamicEditableAnchorRegions_,
            DashboardActiveRegionKind::DynamicEditAnchorHandle,
            DashboardActiveRegionKind::DynamicEditAnchorTarget);

        const auto appendColorRegions = [&](const std::vector<LayoutEditColorRegion>& regions,
                                            DashboardActiveRegionKind kind) {
            for (const auto& region : regions) {
                appendRegion(region.targetRect, kind, &region);
            }
        };
        appendColorRegions(layoutResolver_->staticColorEditRegions_, DashboardActiveRegionKind::StaticColorTarget);
        appendColorRegions(layoutResolver_->dynamicColorEditRegions_, DashboardActiveRegionKind::DynamicColorTarget);
    }

    return regions;
}

void DashboardRenderer::WriteScreenshotActiveRegionsTrace(const DashboardOverlayState& overlayState) const {
    const std::vector<DashboardActiveRegion> regions = CollectActiveRegions(overlayState);
    for (const DashboardActiveRegion& region : regions) {
        WriteTrace("diagnostics:active_region box=" + FormatTraceRect(region.box) +
                   " visual_type=" + Trace::QuoteText(FormatActiveRegionVisualType(region.kind)) +
                   " path=" + Trace::QuoteText(FormatActiveRegionPath(config_, region)) +
                   " detail=" + Trace::QuoteText(FormatActiveRegionDetail(config_, region)));
    }

    WriteTrace("diagnostics:active_regions count=" + std::to_string(regions.size()) +
               " layout_edit=" + Trace::BoolText(overlayState.showLayoutEditGuides));
}

int DashboardRenderer::ScaleLogical(int value) const {
    return renderer_->ScaleLogical(value);
}

std::vector<LayoutGuideSnapCandidate> DashboardRenderer::CollectLayoutGuideSnapCandidates(
    const LayoutEditGuide& guide) const {
    struct SimilarityTypeKey {
        WidgetClass widgetClass = WidgetClass::Unknown;
        int extent = 0;

        bool operator<(const SimilarityTypeKey& other) const {
            if (widgetClass != other.widgetClass) {
                return widgetClass < other.widgetClass;
            }
            return extent < other.extent;
        }
    };

    std::vector<const WidgetLayout*> allWidgets = CollectSimilarityIndicatorWidgets(guide.axis);
    std::vector<const WidgetLayout*> affectedWidgets;
    for (const WidgetLayout* widget : allWidgets) {
        if (IsWidgetAffectedByGuide(*widget, guide)) {
            affectedWidgets.push_back(widget);
        }
    }

    std::vector<LayoutGuideSnapCandidate> candidates;
    for (const WidgetLayout* affected : affectedWidgets) {
        const int startExtent = WidgetExtentForAxis(*affected, guide.axis);
        if (startExtent <= 0 || affected->widget == nullptr) {
            continue;
        }
        std::set<SimilarityTypeKey> seenTargets;
        for (size_t i = 0; i < allWidgets.size(); ++i) {
            const WidgetLayout* target = allWidgets[i];
            if (target == affected || target->widget == nullptr ||
                target->widget->Class() != affected->widget->Class()) {
                continue;
            }
            const SimilarityTypeKey typeKey{target->widget->Class(), WidgetExtentForAxis(*target, guide.axis)};
            if (!seenTargets.insert(typeKey).second) {
                continue;
            }
            candidates.push_back(LayoutGuideSnapCandidate{
                {affected->cardId, affected->editCardId, affected->nodePath},
                typeKey.extent,
                startExtent,
                std::abs(typeKey.extent - startExtent),
                i,
            });
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (left.startDistance != right.startDistance) {
            return left.startDistance < right.startDistance;
        }
        return left.groupOrder < right.groupOrder;
    });
    return candidates;
}

std::optional<int> DashboardRenderer::FindLayoutWidgetExtent(
    const LayoutEditWidgetIdentity& identity, LayoutGuideAxis axis) const {
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (MatchesWidgetIdentity(widget, identity)) {
                return WidgetExtentForAxis(widget, axis);
            }
        }
    }
    return std::nullopt;
}

bool DashboardRenderer::ApplyLayoutGuideWeightsPreview(
    const std::string& editCardId, const std::vector<size_t>& nodePath, const std::vector<int>& weights) {
    LayoutEditLayoutTarget target;
    target.editCardId = editCardId;
    target.nodePath = nodePath;
    if (!ApplyGuideWeights(config_, target, weights)) {
        return false;
    }
    return ResolveLayout(false);
}

const MetricDefinitionConfig* DashboardRenderer::FindConfiguredMetricDefinition(std::string_view metricRef) const {
    const std::string key(metricRef);
    const auto cached = metricDefinitionCache_.find(key);
    if (cached != metricDefinitionCache_.end()) {
        return cached->second;
    }
    const MetricDefinitionConfig* definition = FindEffectiveMetricDefinition(config_.layout.metrics, metricRef);
    metricDefinitionCache_.emplace(key, definition);
    return definition;
}

const std::string& DashboardRenderer::ResolveConfiguredMetricSampleValueText(std::string_view metricRef) const {
    const std::string key(metricRef);
    const auto cached = metricSampleValueTextCache_.find(key);
    if (cached != metricSampleValueTextCache_.end()) {
        return cached->second;
    }
    return metricSampleValueTextCache_.emplace(key, ResolveMetricSampleValueText(config_.layout.metrics, key))
        .first->second;
}

std::optional<LayoutEditWidgetIdentity> DashboardRenderer::HitTestLayoutCard(RenderPoint clientPoint) const {
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        if (card.rect.Contains(clientPoint)) {
            return LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
        }
    }
    return std::nullopt;
}

std::optional<LayoutEditWidgetIdentity> DashboardRenderer::HitTestEditableCard(RenderPoint clientPoint) const {
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        if (!card.rect.Contains(clientPoint) || clientPoint.y > card.chromeLayout.contentRect.top) {
            continue;
        }
        return LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
    }
    return std::nullopt;
}

std::optional<LayoutEditWidgetIdentity> DashboardRenderer::HitTestEditableWidget(RenderPoint clientPoint) const {
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.widget == nullptr || !widget.widget->IsHoverable() || !widget.rect.Contains(clientPoint)) {
                continue;
            }
            return LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        }
    }
    return std::nullopt;
}

std::optional<LayoutEditGapAnchorKey> DashboardRenderer::HitTestGapEditAnchor(RenderPoint clientPoint) const {
    const LayoutEditGapAnchor* bestAnchor = nullptr;
    int bestPriority = 0;
    for (auto it = layoutResolver_->gapEditAnchors_.rbegin(); it != layoutResolver_->gapEditAnchors_.rend(); ++it) {
        if (!it->hitRect.Contains(clientPoint)) {
            continue;
        }

        const int priority = GetLayoutEditParameterHitPriority(it->key.parameter);
        if (bestAnchor == nullptr || priority < bestPriority) {
            bestAnchor = &(*it);
            bestPriority = priority;
        }
    }
    return bestAnchor != nullptr ? std::optional<LayoutEditGapAnchorKey>(bestAnchor->key) : std::nullopt;
}

std::optional<LayoutEditAnchorKey> DashboardRenderer::HitTestEditableAnchorTarget(RenderPoint clientPoint) const {
    std::vector<const LayoutEditAnchorRegion*> regions;
    regions.reserve(
        layoutResolver_->staticEditableAnchorRegions_.size() + layoutResolver_->dynamicEditableAnchorRegions_.size());
    for (const auto& region : layoutResolver_->staticEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    for (const auto& region : layoutResolver_->dynamicEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    const LayoutEditAnchorRegion* bestRegion = nullptr;
    long long bestArea = (std::numeric_limits<long long>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!(*it)->targetRect.Contains(clientPoint)) {
            continue;
        }

        const long long width = (std::max<LONG>)(0, (*it)->targetRect.right - (*it)->targetRect.left);
        const long long height = (std::max<LONG>)(0, (*it)->targetRect.bottom - (*it)->targetRect.top);
        const long long area = width * height;
        if (bestRegion == nullptr || area < bestArea) {
            bestRegion = *it;
            bestArea = area;
        }
    }
    return bestRegion != nullptr ? std::optional<LayoutEditAnchorKey>(bestRegion->key) : std::nullopt;
}

std::optional<LayoutEditAnchorKey> DashboardRenderer::HitTestEditableAnchorHandle(RenderPoint clientPoint) const {
    std::vector<const LayoutEditAnchorRegion*> regions;
    regions.reserve(
        layoutResolver_->staticEditableAnchorRegions_.size() + layoutResolver_->dynamicEditableAnchorRegions_.size());
    for (const auto& region : layoutResolver_->staticEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    for (const auto& region : layoutResolver_->dynamicEditableAnchorRegions_) {
        regions.push_back(&region);
    }
    const LayoutEditAnchorRegion* bestRegion = nullptr;
    int bestPriority = 0;
    const auto hoverPriority = [](const LayoutEditAnchorRegion& region) {
        if (region.shape == AnchorShape::Square) {
            return -2;
        }
        if (region.shape == AnchorShape::Wedge) {
            return 2;
        }
        return LayoutEditAnchorHitPriority(region.key);
    };
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        const LayoutEditAnchorRegion& region = *(*it);
        bool hit = false;
        if (region.shape == AnchorShape::Circle) {
            const int width = std::max(1, region.anchorRect.right - region.anchorRect.left);
            const int height = std::max(1, region.anchorRect.bottom - region.anchorRect.top);
            const double radius = static_cast<double>(std::max(width, height)) / 2.0;
            const double centerX = static_cast<double>(region.anchorRect.left) + static_cast<double>(width) / 2.0;
            const double centerY = static_cast<double>(region.anchorRect.top) + static_cast<double>(height) / 2.0;
            const double dx = static_cast<double>(clientPoint.x) - centerX;
            const double dy = static_cast<double>(clientPoint.y) - centerY;
            const double distance = std::sqrt((dx * dx) + (dy * dy));
            hit = std::abs(distance - radius) <= static_cast<double>(region.anchorHitPadding);
        } else {
            hit = region.anchorHitRect.Contains(clientPoint);
        }
        if (!hit) {
            continue;
        }

        const int priority = hoverPriority(region);
        if (bestRegion == nullptr || priority < bestPriority) {
            bestRegion = &region;
            bestPriority = priority;
        }
    }
    return bestRegion != nullptr ? std::optional<LayoutEditAnchorKey>(bestRegion->key) : std::nullopt;
}

std::optional<LayoutEditAnchorRegion> DashboardRenderer::FindEditableAnchorRegion(
    const LayoutEditAnchorKey& key) const {
    const auto findIn =
        [&](const std::vector<LayoutEditAnchorRegion>& regions) -> std::optional<LayoutEditAnchorRegion> {
        const auto it = std::find_if(regions.begin(), regions.end(), [&](const LayoutEditAnchorRegion& region) {
            return MatchesEditableAnchorKey(region.key, key);
        });
        if (it == regions.end()) {
            return std::nullopt;
        }
        return *it;
    };
    if (const auto staticRegion = findIn(layoutResolver_->staticEditableAnchorRegions_); staticRegion.has_value()) {
        return staticRegion;
    }
    return findIn(layoutResolver_->dynamicEditableAnchorRegions_);
}

std::optional<LayoutEditColorRegion> DashboardRenderer::HitTestEditableColorRegion(RenderPoint clientPoint) const {
    std::vector<const LayoutEditColorRegion*> regions;
    regions.reserve(layoutResolver_->staticColorEditRegions_.size() + layoutResolver_->dynamicColorEditRegions_.size());
    for (const auto& region : layoutResolver_->staticColorEditRegions_) {
        regions.push_back(&region);
    }
    for (const auto& region : layoutResolver_->dynamicColorEditRegions_) {
        regions.push_back(&region);
    }
    const LayoutEditColorRegion* bestRegion = nullptr;
    int bestPriority = (std::numeric_limits<int>::max)();
    long long bestArea = (std::numeric_limits<long long>::max)();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!(*it)->targetRect.Contains(clientPoint)) {
            continue;
        }

        const int priority = GetLayoutEditParameterHitPriority((*it)->parameter);
        const long long width = (std::max<LONG>)(0, (*it)->targetRect.right - (*it)->targetRect.left);
        const long long height = (std::max<LONG>)(0, (*it)->targetRect.bottom - (*it)->targetRect.top);
        const long long area = width * height;
        if (bestRegion == nullptr || priority < bestPriority || (priority == bestPriority && area < bestArea)) {
            bestRegion = *it;
            bestPriority = priority;
            bestArea = area;
        }
    }
    return bestRegion != nullptr ? std::optional<LayoutEditColorRegion>(*bestRegion) : std::nullopt;
}

std::optional<LayoutEditGapAnchor> DashboardRenderer::FindGapEditAnchor(const LayoutEditGapAnchorKey& key) const {
    const auto it = std::find_if(layoutResolver_->gapEditAnchors_.begin(),
        layoutResolver_->gapEditAnchors_.end(),
        [&](const LayoutEditGapAnchor& anchor) { return MatchesGapEditAnchorKey(anchor.key, key); });
    if (it == layoutResolver_->gapEditAnchors_.end()) {
        return std::nullopt;
    }
    return *it;
}

std::optional<LayoutEditWidgetIdentity> DashboardRenderer::FindFirstLayoutEditPreviewWidget(
    const std::string& widgetTypeName) const {
    const std::string normalizedName = ToLower(Trim(widgetTypeName));
    const auto widgetClass = normalizedName.empty() ? std::nullopt : EnumFromString<WidgetClass>(normalizedName);
    if (!widgetClass.has_value()) {
        return std::nullopt;
    }

    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.widget == nullptr || !widget.widget->IsHoverable() || widget.widget->Class() != *widgetClass) {
                continue;
            }
            return LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        }
    }
    return std::nullopt;
}

bool DashboardRenderer::Initialize(HWND hwnd) {
    lastError_.clear();
    renderer_->AttachWindow(hwnd);
    if (!renderer_->SetStyle(BuildRendererStyle()) || !ResolveLayout()) {
        lastError_ = renderer_->LastError().empty() ? "renderer:initialize_failed" : renderer_->LastError();
        return false;
    }
    return true;
}

void DashboardRenderer::Shutdown() {
    InvalidateMetricSourceCache();
    layoutResolver_->Clear();
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    renderer_->Shutdown();
}

void DashboardRenderer::DiscardWindowRenderTarget(std::string_view reason) {
    renderer_->DiscardWindowTarget(reason);
}

RendererStyle DashboardRenderer::BuildRendererStyle() const {
    RendererStyle style;
    style.colors = config_.layout.colors;
    style.fonts = config_.layout.fonts;
    style.scale = renderScale_;
    for (const auto& card : config_.layout.cards) {
        if (!card.icon.empty()) {
            style.iconNames.push_back(card.icon);
        }
    }
    return style;
}

const MetricSource& DashboardRenderer::ResolveMetrics(const SystemSnapshot& snapshot) {
    if (cachedMetricSource_ == nullptr || cachedMetricSnapshot_ != &snapshot ||
        cachedMetricSnapshotRevision_ != snapshot.revision) {
        cachedMetricSource_ = std::make_unique<MetricSource>(snapshot, config_.layout.metrics);
        cachedMetricSnapshot_ = &snapshot;
        cachedMetricSnapshotRevision_ = snapshot.revision;
    }
    return *cachedMetricSource_;
}

void DashboardRenderer::InvalidateMetricSourceCache() {
    cachedMetricSource_.reset();
    cachedMetricSnapshot_ = nullptr;
    cachedMetricSnapshotRevision_ = 0;
}

void DashboardRenderer::WriteTrace(const std::string& text) const {
    if (interactiveDragTraceActive_ && text.rfind("renderer:", 0) == 0) {
        return;
    }
    trace_.Write(text);
}

int DashboardRenderer::WidgetExtentForAxis(const WidgetLayout& widget, LayoutGuideAxis axis) const {
    return axis == LayoutGuideAxis::Vertical ? std::max(0, static_cast<int>(widget.rect.right - widget.rect.left))
                                             : std::max(0, static_cast<int>(widget.rect.bottom - widget.rect.top));
}

bool DashboardRenderer::IsWidgetAffectedByGuide(const WidgetLayout& widget, const LayoutEditGuide& guide) const {
    if (!guide.renderCardId.empty() && widget.cardId != guide.renderCardId) {
        return false;
    }
    return widget.rect.left >= guide.containerRect.left && widget.rect.top >= guide.containerRect.top &&
           widget.rect.right <= guide.containerRect.right && widget.rect.bottom <= guide.containerRect.bottom;
}

bool DashboardRenderer::MatchesWidgetIdentity(
    const WidgetLayout& widget, const LayoutEditWidgetIdentity& identity) const {
    return identity.kind == LayoutEditWidgetIdentity::Kind::Widget && widget.cardId == identity.renderCardId &&
           widget.editCardId == identity.editCardId && widget.nodePath == identity.nodePath;
}

bool DashboardRenderer::SupportsLayoutSimilarityIndicator(const WidgetLayout& widget) const {
    if (widget.widget == nullptr || widget.widget->IsVerticalSpring()) {
        return false;
    }
    if (UsesFixedPreferredHeightInRows(widget)) {
        return false;
    }
    return true;
}

std::vector<const WidgetLayout*> DashboardRenderer::CollectSimilarityIndicatorWidgets(LayoutGuideAxis axis) const {
    struct SimilarityRepresentativeKey {
        std::string cardId;
        WidgetClass widgetClass = WidgetClass::Unknown;
        int extent = 0;
        int edgeStart = 0;
        int edgeEnd = 0;

        bool operator==(const SimilarityRepresentativeKey& other) const {
            return cardId == other.cardId && widgetClass == other.widgetClass && extent == other.extent &&
                   edgeStart == other.edgeStart && edgeEnd == other.edgeEnd;
        }
    };

    struct SimilarityRepresentativeKeyHash {
        size_t operator()(const SimilarityRepresentativeKey& key) const {
            size_t hash = std::hash<std::string>{}(key.cardId);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(static_cast<int>(key.widgetClass));
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.extent);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.edgeStart);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.edgeEnd);
            return hash;
        }
    };

    std::vector<const WidgetLayout*> widgets;
    std::unordered_set<SimilarityRepresentativeKey, SimilarityRepresentativeKeyHash> seenKeys;
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (!SupportsLayoutSimilarityIndicator(widget) || widget.widget == nullptr) {
                continue;
            }

            const int extent = WidgetExtentForAxis(widget, axis);
            if (extent <= 0) {
                continue;
            }

            SimilarityRepresentativeKey key;
            key.cardId = widget.cardId;
            key.widgetClass = widget.widget->Class();
            key.extent = extent;
            if (axis == LayoutGuideAxis::Vertical) {
                key.edgeStart = widget.rect.left;
                key.edgeEnd = widget.rect.right;
            } else {
                key.edgeStart = widget.rect.top;
                key.edgeEnd = widget.rect.bottom;
            }
            if (!seenKeys.insert(std::move(key)).second) {
                continue;
            }
            widgets.push_back(&widget);
        }
    }
    return widgets;
}

bool DashboardRenderer::IsContainerNode(const LayoutNodeConfig& node) {
    return node.name == "rows" || node.name == "columns";
}

bool DashboardRenderer::UsesFixedPreferredHeightInRows(const WidgetLayout& widget) const {
    return widget.widget != nullptr && widget.widget->UsesFixedPreferredHeightInRows();
}

const LayoutCardConfig* DashboardRenderer::FindCardConfigById(const std::string& id) const {
    const auto it = std::find_if(
        config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) { return card.id == id; });
    return it != config_.layout.cards.end() ? &(*it) : nullptr;
}
