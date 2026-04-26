#include "dashboard_renderer/dashboard_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dashboard_renderer/impl/layout_edit_overlay_renderer.h"
#include "dashboard_renderer/impl/layout_resolver.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_service.h"
#include "util/strings.h"
#include "util/trace.h"

namespace {

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

}  // namespace

DashboardRenderer::DashboardRenderer(Trace& trace)
    : trace_(trace), renderer_(CreateRenderer()), layoutResolver_(std::make_unique<DashboardLayoutResolver>(*this)),
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

WidgetEditArtifactRegistrar& DashboardRenderer::EditArtifacts() {
    return *layoutResolver_;
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

LayoutEditActiveRegions DashboardRenderer::CollectLayoutEditActiveRegions(
    const DashboardOverlayState& overlayState) const {
    LayoutEditActiveRegions regions;
    const auto appendRegion =
        [&](const RenderRect& box, LayoutEditActiveRegionKind kind, LayoutEditActiveRegionPayload payload) {
            if (box.IsEmpty()) {
                return;
            }
            regions.Add(LayoutEditActiveRegion{box, kind, std::move(payload)});
        };

    if (overlayState.showLayoutEditGuides) {
        for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
            LayoutEditCardRegion cardRegion{
                card.id, card.nodePath, card.rect, card.chromeLayout.titleRect, card.chromeLayout.hasHeader};
            appendRegion(card.rect, LayoutEditActiveRegionKind::Card, cardRegion);
            if (card.chromeLayout.hasHeader) {
                appendRegion(card.chromeLayout.titleRect, LayoutEditActiveRegionKind::CardHeader, cardRegion);
            }
            for (const auto& widget : card.widgets) {
                if (widget.widget == nullptr || !widget.widget->IsHoverable()) {
                    continue;
                }
                appendRegion(widget.rect,
                    LayoutEditActiveRegionKind::WidgetHover,
                    LayoutEditWidgetRegion{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                        widget.widget->Class(),
                        widget.rect,
                        SupportsLayoutSimilarityIndicator(widget)});
            }
        }

        for (const auto& guide : layoutResolver_->layoutEditGuides_) {
            appendRegion(guide.hitRect, LayoutEditActiveRegionKind::LayoutWeightGuide, guide);
        }

        for (const auto& target : layoutResolver_->containerChildReorderTargets_) {
            LayoutEditContainerChildReorderRegion targetRegion{
                target.renderCardId, target.editCardId, target.nodePath, target.horizontal, target.childRects};
            for (const RenderRect& childRect : target.childRects) {
                appendRegion(childRect, LayoutEditActiveRegionKind::ContainerChildReorderTarget, targetRegion);
            }
        }

        for (const auto& anchor : layoutResolver_->gapEditAnchors_) {
            appendRegion(anchor.hitRect, LayoutEditActiveRegionKind::GapHandle, anchor);
        }

        for (const auto& guide : layoutResolver_->widgetEditGuides_) {
            appendRegion(guide.hitRect, LayoutEditActiveRegionKind::WidgetGuide, guide);
        }

        const auto appendAnchorRegions = [&](const std::vector<LayoutEditAnchorRegion>& regions,
                                             LayoutEditActiveRegionKind handleKind,
                                             LayoutEditActiveRegionKind targetKind) {
            for (const auto& region : regions) {
                appendRegion(region.anchorHitRect, handleKind, region);
                appendRegion(region.targetRect, targetKind, region);
            }
        };
        appendAnchorRegions(layoutResolver_->staticEditableAnchorRegions_,
            LayoutEditActiveRegionKind::StaticEditAnchorHandle,
            LayoutEditActiveRegionKind::StaticEditAnchorTarget);
        appendAnchorRegions(layoutResolver_->dynamicEditableAnchorRegions_,
            LayoutEditActiveRegionKind::DynamicEditAnchorHandle,
            LayoutEditActiveRegionKind::DynamicEditAnchorTarget);

        const auto appendColorRegions = [&](const std::vector<LayoutEditColorRegion>& regions,
                                            LayoutEditActiveRegionKind kind) {
            for (const auto& region : regions) {
                appendRegion(region.targetRect, kind, region);
            }
        };
        appendColorRegions(layoutResolver_->staticColorEditRegions_, LayoutEditActiveRegionKind::StaticColorTarget);
        appendColorRegions(layoutResolver_->dynamicColorEditRegions_, LayoutEditActiveRegionKind::DynamicColorTarget);
    }

    return regions;
}

int DashboardRenderer::ScaleLogical(int value) const {
    return renderer_->ScaleLogical(value);
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
