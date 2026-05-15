#include "dashboard_renderer/dashboard_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "dashboard_renderer/impl/layout_edit_overlay_renderer.h"
#include "dashboard_renderer/impl/layout_resolver.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_hit_priority.h"
#include "layout_model/layout_edit_service.h"
#include "util/strings.h"
#include "util/trace.h"

DashboardRenderer::DashboardRenderer(Trace& trace)
    : trace_(trace), renderer_(CreateRenderer()), layoutResolver_(std::make_unique<DashboardLayoutResolver>(*this)),
      layoutEditOverlayRenderer_(std::make_unique<DashboardLayoutEditOverlayRenderer>(*this, *layoutResolver_)),
      layerBitmapPool_(std::make_shared<DashboardLayerBitmapPool>()) {
    presentation_.SetTrace(&trace_);
    presentation_.SetBitmapPool(layerBitmapPool_);
}

DashboardRenderer::~DashboardRenderer() {
    Shutdown();
}

namespace {

int AnchorHoverPriority(const LayoutEditAnchorRegion& region) {
    if (region.shape == AnchorShape::Wedge) {
        return -3;
    }
    if (region.shape == AnchorShape::Square) {
        return -2;
    }
    return LayoutEditAnchorHitPriority(region.key);
}

bool AnchorHandleContains(const LayoutEditAnchorRegion& region, RenderPoint clientPoint) {
    if (!region.anchorHitRect.Contains(clientPoint) &&
        !region.anchorRect.Inflate(region.anchorHitPadding, region.anchorHitPadding).Contains(clientPoint)) {
        return false;
    }
    if (region.shape != AnchorShape::Circle) {
        return true;
    }

    const int width = std::max(1, region.anchorRect.right - region.anchorRect.left);
    const int height = std::max(1, region.anchorRect.bottom - region.anchorRect.top);
    const double radius = static_cast<double>(std::max(width, height)) / 2.0;
    const double centerX = static_cast<double>(region.anchorRect.left) + static_cast<double>(width) / 2.0;
    const double centerY = static_cast<double>(region.anchorRect.top) + static_cast<double>(height) / 2.0;
    const double dx = static_cast<double>(clientPoint.x) - centerX;
    const double dy = static_cast<double>(clientPoint.y) - centerY;
    const double distance = std::sqrt((dx * dx) + (dy * dy));
    return std::abs(distance - radius) <= static_cast<double>(region.anchorHitPadding);
}

long long RectArea(const RenderRect& rect) {
    const long long width = std::max<long long>(0, rect.right - rect.left);
    const long long height = std::max<long long>(0, rect.bottom - rect.top);
    return width * height;
}

}  // namespace

void DashboardRenderer::SetConfig(const AppConfig& config) {
    lastError_.clear();
    ++configVersion_;
    const bool metricsChanged = config_.layout.metrics != config.layout.metrics;
    if (metricsChanged) {
        InvalidateMetricSourceCache();
        metricLookupCache_.Clear();
    }
    config_ = config;
    const RendererStyle nextStyle = BuildRendererStyle();
    if (!renderer_->SetStyle(nextStyle) || !ResolveLayout()) {
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
    ++configVersion_;
    if (!renderer_->SetStyle(BuildRendererStyle()) || !ResolveLayout()) {
        lastError_ = renderer_->LastError().empty() ? "renderer:rescale_failed" : renderer_->LastError();
    }
}

void DashboardRenderer::SetImmediatePresent(bool enabled) {
    if (immediatePresent_ == enabled) {
        return;
    }
    immediatePresent_ = enabled;
    snapshotLayerValid_ = false;
    overlayLayerVisible_ = false;
    renderer_->AttachWindow(immediatePresent_ ? presentationHwnd_ : nullptr);
    renderer_->SetImmediatePresent(enabled);
    renderer_->SetHardwareLayerBitmaps(presentationHwnd_ != nullptr);
    presentation_.Configure(presentationHwnd_, presentationHwnd_ != nullptr && !immediatePresent_, immediatePresent_);
    ClearReusableLayerBitmaps();
}

void DashboardRenderer::SetLiveAnimationEnabled(bool enabled) {
    if (liveAnimationEnabled_ == enabled) {
        return;
    }
    liveAnimationEnabled_ = enabled;
    if (!liveAnimationEnabled_) {
        WriteTrace("animation_timeline_reset_request reason=live_animation_disabled");
        presentation_.ResetTimeline();
    }
}

void DashboardRenderer::SetRenderMode(RenderMode mode) {
    if (renderMode_ == mode) {
        return;
    }
    renderMode_ = mode;
    snapshotLayerValid_ = false;
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
    if (!lastError_.empty()) {
        return lastError_;
    }
    const std::string presentationError = presentation_.LastError();
    if (!presentationError.empty()) {
        lastError_ = presentationError;
        return lastError_;
    }
    return renderer_->LastError();
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

void DashboardRenderer::AddWidgetAnimation(WidgetAnimationPtr animation, WidgetAnimationStatePtr targetState) {
    if (animation == nullptr || targetState == nullptr) {
        return;
    }
    if (!widgetAnimationCollectionActive_) {
        WidgetHost::AddWidgetAnimation(std::move(animation), std::move(targetState));
        return;
    }
    WidgetAnimationsForLayer(currentWidgetAnimationLayer_)
        .push_back(DashboardPresentationAnimation{
            std::move(animation), std::move(targetState), currentWidgetAnimationTranslation_});
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
    const std::vector<size_t>& nodePath,
    const std::vector<LayoutEditOverlayOwner>& overlayOwners) {
    layoutResolver_->AddLayoutEditGuide(
        *this, node, rect, childRects, gap, renderCardId, editCardId, nodePath, overlayOwners);
}

void DashboardRenderer::ResolveNodeWidgetsInternal(const LayoutNodeConfig& node,
    const RenderRect& rect,
    std::vector<WidgetLayout>& widgets,
    std::vector<std::string>& cardReferenceStack,
    const std::string& renderCardId,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath,
    bool instantiateWidgets) {
    std::vector<LayoutEditOverlayOwner> overlayOwners;
    layoutResolver_->ResolveNodeWidgetsInternal(*this,
        node,
        rect,
        widgets,
        cardReferenceStack,
        overlayOwners,
        renderCardId,
        editCardId,
        nodePath,
        instantiateWidgets);
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
    binding.drag = LayoutEditAnchorDragSpec::AxisDelta(AnchorDragAxis::Vertical);
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
    binding.drag = std::nullopt;
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
    layoutResolver_->SetEditArtifactContext(widget.overlayOwners,
        currentWidgetAnimationLayer_ == WidgetAnimationLayer::Overlay ? LayoutEditOverlayAffordanceLayer::Foreground
                                                                      : LayoutEditOverlayAffordanceLayer::Background);
    widget.widget->Draw(*this, widget, metrics);
    layoutResolver_->ResetEditArtifactContext();
}

void DashboardRenderer::DrawResolvedWidgetOverlay(const WidgetLayout& widget, const MetricSource& metrics) {
    if (widget.widget == nullptr) {
        return;
    }
    layoutResolver_->SetEditArtifactContext(widget.overlayOwners, LayoutEditOverlayAffordanceLayer::Foreground);
    widget.widget->DrawOverlay(*this, widget, metrics);
    layoutResolver_->ResetEditArtifactContext();
}

bool DashboardRenderer::DrawWindow(const SystemSnapshot& snapshot) {
    return DrawWindow(snapshot, DashboardOverlayState{});
}

bool DashboardRenderer::DrawWindow(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    lastError_.clear();
    DashboardPresentationFrame frame;
    {
        auto timing = trace_.Timings().Measure(trace_, "presentation_frame_build");
        if (!BuildPresentationFrame(snapshot, overlayState, frame)) {
            return false;
        }
    }
    const bool presented = [&] {
        auto timing = trace_.Timings().Measure(trace_, "presentation_frame_publish");
        return immediatePresent_ && presentationHwnd_ != nullptr
                   ? presentation_.PresentFrameSynchronously(*renderer_, std::move(frame))
               : presentationHwnd_ == nullptr ? presentation_.PresentFrameSynchronously(std::move(frame))
                                              : presentation_.PublishFrame(std::move(frame));
    }();
    if (!presented) {
        lastError_ = presentation_.LastError();
    }
    return presented && lastError_.empty();
}

bool DashboardRenderer::BuildPresentationFrame(
    const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState, DashboardPresentationFrame& frame) {
    BeginWidgetAnimationCollection();
    activeOverlayState_ = &overlayState;

    frame = {};
    frame.width = WindowWidth();
    frame.height = WindowHeight();
    frame.style = BuildRendererStyle();
    const std::uint64_t surfaceVersion = ResolveSurfaceVersion();
    const std::string overlaySignature = SnapshotOverlaySignature(overlayState);
    const bool updateSnapshot = ShouldUpdateSnapshotLayer(snapshot, overlayState, surfaceVersion);
    const bool overlayVisible = overlayState.ShouldDrawOverlayLayer();
    const bool updateOverlay = overlayVisible || overlayLayerVisible_;

    frame.versions.surfaceVersion = surfaceVersion;
    frame.versions.snapshotVersion = snapshotVersion_;
    frame.versions.overlayVersion = overlayVersion_;
    frame.versions.metricVersion = snapshot.revision;
    frame.animate = liveAnimationEnabled_ && renderMode_ == RenderMode::Normal;
    frame.snapshotLayerUpdated = updateSnapshot;
    frame.overlayLayerUpdated = updateOverlay;

    const MetricSource* metrics = nullptr;
    {
        auto timing = trace_.Timings().Measure(trace_, "presentation_resolve_metrics");
        metrics = &ResolveMetrics(snapshot);
    }
    if (updateSnapshot) {
        layoutResolver_->ClearDynamicEditArtifacts();
        layoutResolver_->dynamicAnchorRegistrationEnabled_ = overlayState.ShouldRegisterDynamicEditArtifacts();
        BeginWidgetAnimationLayer(WidgetAnimationLayer::Snapshot);
        if (CanReuseLayerBitmaps()) {
            frame.snapshotLayer = AcquireLayerBitmap(frame.width, frame.height);
        }
        const bool snapshotDrawn = [&] {
            auto timing = trace_.Timings().Measure(trace_, "snapshot_layer_bitmap");
            return renderer_->DrawToBitmap(
                frame.snapshotLayer, frame.width, frame.height, RenderBitmapClear::Background, [&] {
                    auto contentTiming = trace_.Timings().Measure(trace_, "snapshot_layer_content");
                    DrawSnapshotLayer(overlayState, *metrics);
                });
        }();
        if (!snapshotDrawn) {
            lastError_ = renderer_->LastError();
            RecycleFrameLayers(std::move(frame));
            EndWidgetAnimationCollection();
            layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
            activeOverlayState_ = nullptr;
            return false;
        }
        frame.versions.snapshotVersion = ++snapshotVersion_;
        frame.snapshotAnimations = std::move(snapshotWidgetAnimations_);
        MarkSnapshotLayerUpdated(snapshot, overlaySignature, surfaceVersion);
        layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    }

    {
        auto timing = trace_.Timings().Measure(trace_, "dynamic_edit_collisions");
        layoutResolver_->ResolveDynamicEditArtifactCollisions();
    }
    if (overlayVisible) {
        layoutResolver_->TagOverlayAffordanceLayers(overlayState);
        BeginWidgetAnimationLayer(WidgetAnimationLayer::Overlay);
        RenderBitmap overlayBitmap;
        if (CanReuseLayerBitmaps()) {
            overlayBitmap = AcquireLayerBitmap(frame.width, frame.height);
        }
        const bool overlayDrawn = [&] {
            auto timing = trace_.Timings().Measure(trace_, "overlay_layer_bitmap");
            return renderer_->DrawToBitmap(
                overlayBitmap, frame.width, frame.height, RenderBitmapClear::Transparent, [&] {
                    auto contentTiming = trace_.Timings().Measure(trace_, "overlay_layer_content");
                    DrawOverlayLayerStatic(overlayState, *metrics);
                });
        }();
        if (!overlayDrawn) {
            lastError_ = renderer_->LastError();
            if (layerBitmapPool_ != nullptr) {
                layerBitmapPool_->Release(std::move(overlayBitmap));
            }
            RecycleFrameLayers(std::move(frame));
            EndWidgetAnimationCollection();
            layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
            activeOverlayState_ = nullptr;
            return false;
        }
        frame.overlayLayer = std::move(overlayBitmap);
        frame.versions.overlayVersion = ++overlayVersion_;
        frame.overlayAnimations = std::move(overlayWidgetAnimations_);
        overlayLayerVisible_ = true;
    } else if (updateOverlay) {
        frame.versions.overlayVersion = ++overlayVersion_;
        frame.overlayLayer.reset();
        frame.overlayAnimations.clear();
        overlayLayerVisible_ = false;
    }
    if (updateSnapshot || updateOverlay) {
        frame.versions.animationGeometryVersion = ++animationGeometryVersion_;
    } else {
        frame.versions.animationGeometryVersion = animationGeometryVersion_;
    }

    EndWidgetAnimationCollection();
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    activeOverlayState_ = nullptr;
    return true;
}

void DashboardRenderer::BeginWidgetAnimationCollection() {
    snapshotWidgetAnimations_.clear();
    overlayWidgetAnimations_.clear();
    widgetAnimationTranslationStack_.clear();
    widgetAnimationCollectionActive_ = true;
    currentWidgetAnimationLayer_ = WidgetAnimationLayer::Snapshot;
    currentWidgetAnimationTranslation_ = {};
}

void DashboardRenderer::BeginWidgetAnimationLayer(WidgetAnimationLayer layer) {
    currentWidgetAnimationLayer_ = layer;
}

std::vector<DashboardPresentationAnimation>& DashboardRenderer::WidgetAnimationsForLayer(WidgetAnimationLayer layer) {
    return layer == WidgetAnimationLayer::Overlay ? overlayWidgetAnimations_ : snapshotWidgetAnimations_;
}

std::uint64_t DashboardRenderer::ResolveSurfaceVersion() {
    const int width = WindowWidth();
    const int height = WindowHeight();
    if (presentedWidth_ != width || presentedHeight_ != height || std::abs(presentedScale_ - renderScale_) >= 0.0001) {
        presentedWidth_ = width;
        presentedHeight_ = height;
        presentedScale_ = renderScale_;
        ++surfaceVersion_;
    }
    return surfaceVersion_;
}

std::string DashboardRenderer::SnapshotOverlaySignature(const DashboardOverlayState& overlayState) const {
    std::string signature;
    if (overlayState.activeMetricListReorderDrag.has_value()) {
        const MetricListReorderOverlayState& drag = *overlayState.activeMetricListReorderDrag;
        signature += "metric:";
        signature += drag.widget.renderCardId;
        signature += '|';
        signature += drag.widget.editCardId;
        signature += '|';
        signature += std::to_string(static_cast<int>(drag.widget.kind));
        signature += '|';
        for (size_t pathIndex : drag.widget.nodePath) {
            signature += std::to_string(pathIndex);
            signature += '.';
        }
        signature += ':';
        signature += std::to_string(drag.currentIndex);
    }
    if (overlayState.activeContainerChildReorderDrag.has_value()) {
        const ContainerChildReorderOverlayState& drag = *overlayState.activeContainerChildReorderDrag;
        signature += "container:";
        signature += drag.key.editCardId;
        signature += '|';
        for (size_t pathIndex : drag.key.nodePath) {
            signature += std::to_string(pathIndex);
            signature += '.';
        }
        signature += ':';
        signature += std::to_string(drag.currentIndex);
    }
    return signature;
}

bool DashboardRenderer::ShouldUpdateSnapshotLayer(
    const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState, std::uint64_t surfaceVersion) const {
    return !snapshotLayerValid_ || snapshotLayerMetricVersion_ != snapshot.revision ||
           snapshotLayerConfigVersion_ != configVersion_ || snapshotLayerSurfaceVersion_ != surfaceVersion ||
           snapshotLayerRenderMode_ != renderMode_ ||
           snapshotLayerOverlaySignature_ != SnapshotOverlaySignature(overlayState);
}

void DashboardRenderer::MarkSnapshotLayerUpdated(
    const SystemSnapshot& snapshot, const std::string& overlaySignature, std::uint64_t surfaceVersion) {
    snapshotLayerValid_ = true;
    snapshotLayerMetricVersion_ = snapshot.revision;
    snapshotLayerConfigVersion_ = configVersion_;
    snapshotLayerSurfaceVersion_ = surfaceVersion;
    snapshotLayerRenderMode_ = renderMode_;
    snapshotLayerOverlaySignature_ = overlaySignature;
}

bool DashboardRenderer::CanReuseLayerBitmaps() const {
    return presentationHwnd_ != nullptr && layerBitmapPool_ != nullptr;
}

void DashboardRenderer::ClearReusableLayerBitmaps() {
    if (layerBitmapPool_ != nullptr) {
        layerBitmapPool_->Clear();
    }
}

RenderBitmap DashboardRenderer::AcquireLayerBitmap(int width, int height) const {
    return layerBitmapPool_ != nullptr ? layerBitmapPool_->Acquire(width, height) : RenderBitmap{};
}

void DashboardRenderer::RecycleFrameLayers(DashboardPresentationFrame frame) const {
    if (layerBitmapPool_ == nullptr) {
        return;
    }
    layerBitmapPool_->Release(std::move(frame.snapshotLayer));
    if (frame.overlayLayer.has_value()) {
        layerBitmapPool_->Release(std::move(*frame.overlayLayer));
    }
}

void DashboardRenderer::PushWidgetAnimationTranslation(RenderPoint offset) {
    widgetAnimationTranslationStack_.push_back(currentWidgetAnimationTranslation_);
    currentWidgetAnimationTranslation_.x += offset.x;
    currentWidgetAnimationTranslation_.y += offset.y;
    Renderer().PushTranslation(offset);
}

void DashboardRenderer::PopWidgetAnimationTranslation() {
    Renderer().PopTranslation();
    if (widgetAnimationTranslationStack_.empty()) {
        currentWidgetAnimationTranslation_ = {};
        return;
    }
    currentWidgetAnimationTranslation_ = widgetAnimationTranslationStack_.back();
    widgetAnimationTranslationStack_.pop_back();
}

void DashboardRenderer::DrawAnimationTargets(WidgetAnimationLayer layer) {
    std::vector<DashboardPresentationAnimation>& layerAnimations = WidgetAnimationsForLayer(layer);
    if (layerAnimations.empty()) {
        return;
    }

    std::vector<DashboardPresentationAnimation> animations;
    animations.swap(layerAnimations);
    const bool collectionWasActive = widgetAnimationCollectionActive_;
    widgetAnimationCollectionActive_ = false;
    for (DashboardPresentationAnimation& command : animations) {
        const WidgetAnimationPtr& animation = command.animation;
        if (animation == nullptr || command.targetState == nullptr) {
            continue;
        }
        if (command.translation.x != 0 || command.translation.y != 0) {
            Renderer().PushTranslation(command.translation);
        }
        animation->Draw(Renderer(), *command.targetState);
        if (command.translation.x != 0 || command.translation.y != 0) {
            Renderer().PopTranslation();
        }
    }
    widgetAnimationCollectionActive_ = collectionWasActive;
}

void DashboardRenderer::EndWidgetAnimationCollection() {
    snapshotWidgetAnimations_.clear();
    overlayWidgetAnimations_.clear();
    widgetAnimationTranslationStack_.clear();
    widgetAnimationCollectionActive_ = false;
    currentWidgetAnimationLayer_ = WidgetAnimationLayer::Snapshot;
    currentWidgetAnimationTranslation_ = {};
}

void DashboardRenderer::DrawFrame(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    BeginWidgetAnimationCollection();
    activeOverlayState_ = &overlayState;
    layoutResolver_->ClearDynamicEditArtifacts();
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = overlayState.ShouldRegisterDynamicEditArtifacts();
    const MetricSource& metrics = ResolveMetrics(snapshot);

    BeginWidgetAnimationLayer(WidgetAnimationLayer::Snapshot);
    DrawSnapshotLayer(overlayState, metrics);
    DrawAnimationTargets(WidgetAnimationLayer::Snapshot);
    layoutResolver_->ResolveDynamicEditArtifactCollisions();
    if (overlayState.ShouldDrawOverlayLayer()) {
        layoutResolver_->TagOverlayAffordanceLayers(overlayState);
        BeginWidgetAnimationLayer(WidgetAnimationLayer::Overlay);
        DrawOverlayLayerStatic(overlayState, metrics);
        DrawAnimationTargets(WidgetAnimationLayer::Overlay);
    }

    EndWidgetAnimationCollection();
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    activeOverlayState_ = nullptr;
}

void DashboardRenderer::DrawSnapshotLayer(const DashboardOverlayState& overlayState, const MetricSource& metrics) {
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        if (!layoutEditOverlayRenderer_->ShouldSkipBaseWidget(overlayState, card.chrome)) {
            DrawResolvedWidget(card.chrome, metrics);
        }
        for (const auto& widget : card.widgets) {
            if (!layoutEditOverlayRenderer_->ShouldSkipBaseWidget(overlayState, widget)) {
                DrawResolvedWidget(widget, metrics);
            }
        }
    }
}

void DashboardRenderer::DrawOverlayLayerStatic(const DashboardOverlayState& overlayState, const MetricSource& metrics) {
    layoutEditOverlayRenderer_->DrawBackgroundAffordances(overlayState);
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        DrawResolvedWidgetOverlay(card.chrome, metrics);
        for (const auto& widget : card.widgets) {
            DrawResolvedWidgetOverlay(widget, metrics);
        }
    }
    layoutEditOverlayRenderer_->DrawDraggedContent(metrics);
    layoutEditOverlayRenderer_->DrawForegroundAffordances(overlayState);
    DrawMoveOverlay(overlayState.moveOverlay);
}

bool DashboardRenderer::SaveSnapshotPng(const FilePath& imagePath, const SystemSnapshot& snapshot) {
    return SaveSnapshotPng(imagePath, snapshot, DashboardOverlayState{});
}

bool DashboardRenderer::SaveSnapshotPng(
    const FilePath& imagePath, const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    lastError_.clear();
    const bool previousHardwareLayerBitmaps = renderer_->HardwareLayerBitmapsEnabled();
    renderer_->SetHardwareLayerBitmaps(false);
    DashboardPresentationFrame frame;
    if (!BuildPresentationFrame(snapshot, overlayState, frame)) {
        renderer_->SetHardwareLayerBitmaps(previousHardwareLayerBitmaps);
        return false;
    }
    frame.animate = false;
    const bool saved = renderer_->SavePng(
        imagePath, frame.width, frame.height, [&] { presentation_.DrawFrameForCurrentTarget(*renderer_, frame); });
    if (!renderer_->LastError().empty()) {
        lastError_ = renderer_->LastError();
    }
    RecycleFrameLayers(std::move(frame));
    renderer_->SetHardwareLayerBitmaps(previousHardwareLayerBitmaps);
    return saved;
}

bool DashboardRenderer::BuildAnimationBenchmarkFrame(
    const SystemSnapshot& snapshot, DashboardPresentationFrame& frame) {
    lastError_.clear();
    const bool previousHardwareLayerBitmaps = renderer_->HardwareLayerBitmapsEnabled();
    renderer_->SetHardwareLayerBitmaps(true);
    DashboardOverlayState overlayState;
    const bool built = BuildPresentationFrame(snapshot, overlayState, frame);
    renderer_->SetHardwareLayerBitmaps(previousHardwareLayerBitmaps);
    if (!built) {
        return false;
    }
    frame.animate = true;
    return true;
}

std::vector<LayoutGuideSheetCardSummary> DashboardRenderer::CollectLayoutGuideSheetCardSummaries() const {
    std::vector<LayoutGuideSheetCardSummary> summaries;
    summaries.reserve(layoutResolver_->resolvedLayout_.cards.size());
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        LayoutGuideSheetCardSummary summary;
        summary.id = card.id;
        summary.title = card.title;
        summary.iconName = card.iconName;
        summary.rect = card.rect;
        summary.chromeLayout = card.chromeLayout;
        for (const auto& widget : card.widgets) {
            if (widget.widget != nullptr) {
                summary.widgetClasses.push_back(widget.widgetClass);
            }
        }
        summaries.push_back(std::move(summary));
    }
    return summaries;
}

bool DashboardRenderer::SaveLayoutGuideSheetSurfacePng(
    const FilePath& imagePath, int width, int height, Renderer::DrawCallback draw) {
    return renderer_->SavePng(imagePath, width, height, draw);
}

bool DashboardRenderer::RenderLayoutGuideSheetSurfaceOffscreen(int width, int height, Renderer::DrawCallback draw) {
    return renderer_->DrawOffscreen(width, height, draw);
}

void DashboardRenderer::BeginLayoutGuideSheetDynamicArtifacts(const DashboardOverlayState& overlayState) {
    activeOverlayState_ = &overlayState;
    layoutResolver_->ClearDynamicEditArtifacts();
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = overlayState.ShouldRegisterDynamicEditArtifacts();
}

void DashboardRenderer::ResolveLayoutGuideSheetDynamicArtifactCollisions() {
    layoutResolver_->ResolveDynamicEditArtifactCollisions();
}

void DashboardRenderer::EndLayoutGuideSheetDynamicArtifacts() {
    layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    activeOverlayState_ = nullptr;
}

void DashboardRenderer::DrawLayoutGuideSheetCard(
    const std::string& cardId, const RenderRect& sourceRect, const RenderRect& destRect, const MetricSource& metrics) {
    Renderer().PushClipRect(destRect.Inflate(ScaleLogical(4), ScaleLogical(4)));
    Renderer().PushTranslation(RenderPoint{destRect.left - sourceRect.left, destRect.top - sourceRect.top});
    const auto cardIt = std::find_if(layoutResolver_->resolvedLayout_.cards.begin(),
        layoutResolver_->resolvedLayout_.cards.end(),
        [&](const auto& card) { return card.id == cardId; });
    if (cardIt != layoutResolver_->resolvedLayout_.cards.end()) {
        DrawResolvedWidget(cardIt->chrome, metrics);
        for (const auto& widget : cardIt->widgets) {
            DrawResolvedWidget(widget, metrics);
        }
    }
    Renderer().PopTranslation();
    Renderer().PopClipRect();
}

void DashboardRenderer::DrawLayoutGuideSheetOverlay(const DashboardOverlayState& overlayState,
    const RenderRect& sourceRect,
    const RenderRect& destRect,
    const MetricSource& metrics) {
    Renderer().PushClipRect(destRect.Inflate(ScaleLogical(4), ScaleLogical(4)));
    Renderer().PushTranslation(RenderPoint{destRect.left - sourceRect.left, destRect.top - sourceRect.top});
    layoutEditOverlayRenderer_->Draw(overlayState, metrics);
    Renderer().PopTranslation();
    Renderer().PopClipRect();
}

LayoutGuideSheetCardChromeArtifacts DashboardRenderer::BuildLayoutGuideSheetCardChromeArtifacts(
    const std::string& cardId, const RenderRect& rect, const MetricSource* metrics) {
    LayoutGuideSheetCardChromeArtifacts artifacts;
    const LayoutCardConfig* card = FindCardConfigById(cardId);
    if (card == nullptr) {
        return artifacts;
    }

    WidgetLayout widget;
    widget.rect = rect;
    widget.cardId = cardId;
    widget.editCardId = cardId;
    widget.widget = CreateCardChromeWidget(*card);
    widget.widget->ResolveLayoutState(*this, rect);

    auto savedWidgetGuides = std::move(layoutResolver_->widgetEditGuides_);
    auto savedStaticAnchors = std::move(layoutResolver_->staticEditableAnchorRegions_);
    auto savedDynamicAnchors = std::move(layoutResolver_->dynamicEditableAnchorRegions_);
    auto savedDynamicColors = std::move(layoutResolver_->dynamicColorEditRegions_);
    layoutResolver_->widgetEditGuides_.clear();
    layoutResolver_->staticEditableAnchorRegions_.clear();
    layoutResolver_->dynamicEditableAnchorRegions_.clear();
    layoutResolver_->dynamicColorEditRegions_.clear();

    widget.widget->BuildEditGuides(*this, widget);
    widget.widget->BuildStaticAnchors(*this, widget);
    if (metrics != nullptr) {
        widget.widget->Draw(*this, widget, *metrics);
    }

    artifacts.chromeLayout = ResolveCardChromeLayout(*card, rect, ResolveCardChromeLayoutMetrics(*this));
    artifacts.widgetGuides = layoutResolver_->widgetEditGuides_;
    artifacts.anchorRegions = layoutResolver_->staticEditableAnchorRegions_;
    artifacts.colorRegions = layoutResolver_->dynamicColorEditRegions_;
    if (!card->icon.empty() && !artifacts.chromeLayout.iconRect.IsEmpty() &&
        std::none_of(artifacts.colorRegions.begin(), artifacts.colorRegions.end(), [](const auto& region) {
            return region.parameter == LayoutEditParameter::ColorIcon;
        })) {
        artifacts.colorRegions.push_back(
            LayoutEditColorRegion{LayoutEditParameter::ColorIcon, artifacts.chromeLayout.iconRect});
    }
    if (!card->title.empty() && !artifacts.chromeLayout.titleRect.IsEmpty() &&
        std::none_of(artifacts.colorRegions.begin(), artifacts.colorRegions.end(), [](const auto& region) {
            return region.parameter == LayoutEditParameter::ColorForeground;
        })) {
        const RenderRect titleTextRect =
            Renderer()
                .MeasureTextBlock(artifacts.chromeLayout.titleRect,
                    card->title,
                    TextStyleId::Title,
                    TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center))
                .textRect;
        artifacts.colorRegions.push_back(LayoutEditColorRegion{LayoutEditParameter::ColorForeground, titleTextRect});
    }

    layoutResolver_->widgetEditGuides_ = std::move(savedWidgetGuides);
    layoutResolver_->staticEditableAnchorRegions_ = std::move(savedStaticAnchors);
    layoutResolver_->dynamicEditableAnchorRegions_ = std::move(savedDynamicAnchors);
    layoutResolver_->dynamicColorEditRegions_ = std::move(savedDynamicColors);
    return artifacts;
}

bool DashboardRenderer::RenderSnapshotOffscreen(
    const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    lastError_.clear();
    DashboardPresentationFrame frame;
    if (!BuildPresentationFrame(snapshot, overlayState, frame)) {
        return false;
    }
    frame.animate = false;
    renderer_->DrawOffscreen(
        frame.width, frame.height, [&] { presentation_.DrawFrameForCurrentTarget(*renderer_, frame); });
    if (!renderer_->LastError().empty()) {
        lastError_ = renderer_->LastError();
    }
    const bool rendered = lastError_.empty();
    RecycleFrameLayers(std::move(frame));
    return rendered;
}

bool DashboardRenderer::PrimeLayoutEditDynamicRegions(
    const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState) {
    return RenderSnapshotOffscreen(snapshot, overlayState);
}

bool DashboardRenderer::HasActiveDashboardAnimation() const {
    return liveAnimationEnabled_ && renderMode_ == RenderMode::Normal && presentation_.HasActiveAnimations();
}

LayoutEditActiveRegions DashboardRenderer::CollectLayoutEditActiveRegions(
    const DashboardOverlayState& overlayState) const {
    LayoutEditActiveRegions regions;
    size_t containerChildTargetCount = 0;
    for (const auto& target : layoutResolver_->containerChildReorderTargets_) {
        containerChildTargetCount += target.childRects.size();
    }
    regions.Reserve(
        layoutResolver_->resolvedLayout_.cards.size() * 4 + layoutResolver_->layoutEditGuides_.size() +
        containerChildTargetCount + layoutResolver_->gapEditAnchors_.size() +
        layoutResolver_->widgetEditGuides_.size() +
        (layoutResolver_->staticEditableAnchorRegions_.size() + layoutResolver_->dynamicEditableAnchorRegions_.size()) *
            2 +
        layoutResolver_->staticColorEditRegions_.size() + layoutResolver_->dynamicColorEditRegions_.size());
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
                if (widget.widget == nullptr || !IsWidgetHoverable(widget.widgetClass)) {
                    continue;
                }
                appendRegion(widget.rect,
                    LayoutEditActiveRegionKind::WidgetHover,
                    LayoutEditWidgetRegion{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                        widget.widgetClass,
                        widget.rect,
                        SupportsLayoutSimilarityIndicator(widget)});
            }
        }

        for (const auto& guide : layoutResolver_->layoutEditGuides_) {
            appendRegion(guide.hitRect, LayoutEditActiveRegionKind::LayoutWeightGuide, guide);
        }

        for (const auto& target : layoutResolver_->containerChildReorderTargets_) {
            for (size_t childIndex = 0; childIndex < target.childRects.size(); ++childIndex) {
                const RenderRect& childRect = target.childRects[childIndex];
                appendRegion(childRect,
                    LayoutEditActiveRegionKind::ContainerChildReorderTarget,
                    LayoutEditContainerChildReorderRegion{target.renderCardId,
                        target.editCardId,
                        target.nodePath,
                        target.horizontal,
                        childIndex,
                        childRect});
            }
        }

        for (const auto& anchor : layoutResolver_->gapEditAnchors_) {
            appendRegion(anchor.hitRect, LayoutEditActiveRegionKind::GapHandle, anchor);
        }

        for (const auto& guide : layoutResolver_->widgetEditGuides_) {
            appendRegion(guide.hitRect, LayoutEditActiveRegionKind::WidgetGuide, guide);
        }

        const auto appendAnchorRegions = [&](const std::vector<LayoutEditAnchorRegion>& anchorRegions,
                                             LayoutEditActiveRegionKind handleKind,
                                             LayoutEditActiveRegionKind targetKind) {
            for (const auto& region : anchorRegions) {
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

        const auto appendColorRegions = [&](const std::vector<LayoutEditColorRegion>& colorRegions,
                                            LayoutEditActiveRegionKind kind) {
            for (const auto& region : colorRegions) {
                appendRegion(region.targetRect, kind, region);
            }
        };
        appendColorRegions(layoutResolver_->staticColorEditRegions_, LayoutEditActiveRegionKind::StaticColorTarget);
        appendColorRegions(layoutResolver_->dynamicColorEditRegions_, LayoutEditActiveRegionKind::DynamicColorTarget);
    }

    return regions;
}

LayoutEditHoverResolution DashboardRenderer::ResolveLayoutEditHover(
    const DashboardOverlayState&, RenderPoint clientPoint) const {
    LayoutEditHoverResolution resolution;
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        if (card.rect.Contains(clientPoint)) {
            resolution.hoveredLayoutCard =
                LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
        }
        if (card.chromeLayout.hasHeader && card.chromeLayout.titleRect.Contains(clientPoint)) {
            resolution.hoveredEditableCard =
                LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome};
        }
    }

    const auto hitAnchorHandle = [&](RenderPoint point) -> const LayoutEditAnchorRegion* {
        const LayoutEditAnchorRegion* bestRegion = nullptr;
        int bestPriority = 0;
        const auto scanAnchors = [&](const std::vector<LayoutEditAnchorRegion>& anchors) {
            for (auto it = anchors.rbegin(); it != anchors.rend(); ++it) {
                const LayoutEditAnchorRegion& anchor = *it;
                if (!AnchorHandleContains(anchor, point)) {
                    continue;
                }
                const int priority = AnchorHoverPriority(anchor);
                if (bestRegion == nullptr || priority < bestPriority) {
                    bestRegion = &anchor;
                    bestPriority = priority;
                }
            }
        };
        scanAnchors(layoutResolver_->dynamicEditableAnchorRegions_);
        scanAnchors(layoutResolver_->staticEditableAnchorRegions_);
        return bestRegion;
    };

    const auto hitGapAnchor = [&](RenderPoint point) -> const LayoutEditGapAnchor* {
        const LayoutEditGapAnchor* bestAnchor = nullptr;
        int bestPriority = (std::numeric_limits<int>::max)();
        for (auto it = layoutResolver_->gapEditAnchors_.rbegin(); it != layoutResolver_->gapEditAnchors_.rend(); ++it) {
            const LayoutEditGapAnchor& anchor = *it;
            if (!anchor.hitRect.Contains(point)) {
                continue;
            }
            const int priority = GetLayoutEditParameterHitPriority(anchor.key.parameter);
            if (bestAnchor == nullptr || priority < bestPriority) {
                bestAnchor = &anchor;
                bestPriority = priority;
            }
        }
        return bestAnchor;
    };

    const auto hitWidgetGuide = [&](RenderPoint point) -> const LayoutEditWidgetGuide* {
        const LayoutEditWidgetGuide* bestGuide = nullptr;
        int bestPriority = (std::numeric_limits<int>::max)();
        for (const LayoutEditWidgetGuide& guide : layoutResolver_->widgetEditGuides_) {
            if (!guide.hitRect.Contains(point)) {
                continue;
            }
            const int priority = GetLayoutEditParameterHitPriority(guide.parameter);
            if (bestGuide == nullptr || priority < bestPriority) {
                bestGuide = &guide;
                bestPriority = priority;
            }
        }
        return bestGuide;
    };

    const auto anchorHandle = hitAnchorHandle(clientPoint);
    const auto setHoveredAnchor = [&]() {
        resolution.hoveredEditableAnchor = anchorHandle->key;
        if (anchorHandle->key.widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = anchorHandle->key.widget;
        }
    };
    const auto setActionableAnchor = [&]() {
        setHoveredAnchor();
        if (anchorHandle->draggable) {
            resolution.actionableAnchorHandle = anchorHandle->key;
        }
    };
    const auto gapAnchor = hitGapAnchor(clientPoint);
    const auto widgetGuide = hitWidgetGuide(clientPoint);
    if (anchorHandle != nullptr && gapAnchor != nullptr) {
        const int anchorPriority = LayoutEditAnchorHitPriority(anchorHandle->key);
        const int gapPriority = GetLayoutEditParameterHitPriority(gapAnchor->key.parameter);
        if (anchorPriority <= gapPriority) {
            setActionableAnchor();
            return resolution;
        }
        resolution.hoveredGapEditAnchor = gapAnchor->key;
        resolution.hoveredGapEditAnchorRegion = *gapAnchor;
        resolution.actionableGapEditAnchor = gapAnchor->key;
        return resolution;
    }
    if (anchorHandle != nullptr && widgetGuide != nullptr) {
        const int anchorPriority = LayoutEditAnchorHitPriority(anchorHandle->key);
        const int guidePriority = GetLayoutEditParameterHitPriority(widgetGuide->parameter);
        if (anchorPriority <= guidePriority) {
            setActionableAnchor();
            return resolution;
        }
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuide = *widgetGuide;
        return resolution;
    }
    if (gapAnchor != nullptr && widgetGuide != nullptr) {
        const int gapPriority = GetLayoutEditParameterHitPriority(gapAnchor->key.parameter);
        const int guidePriority = GetLayoutEditParameterHitPriority(widgetGuide->parameter);
        if (gapPriority <= guidePriority) {
            resolution.hoveredGapEditAnchor = gapAnchor->key;
            resolution.hoveredGapEditAnchorRegion = *gapAnchor;
            resolution.actionableGapEditAnchor = gapAnchor->key;
            return resolution;
        }
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuide = *widgetGuide;
        return resolution;
    }
    if (anchorHandle != nullptr) {
        setActionableAnchor();
        return resolution;
    }
    if (gapAnchor != nullptr) {
        resolution.hoveredGapEditAnchor = gapAnchor->key;
        resolution.hoveredGapEditAnchorRegion = *gapAnchor;
        resolution.actionableGapEditAnchor = gapAnchor->key;
        return resolution;
    }
    if (widgetGuide != nullptr) {
        if (widgetGuide->widget.kind == LayoutEditWidgetIdentity::Kind::Widget) {
            resolution.hoveredEditableWidget = widgetGuide->widget;
        }
        resolution.hoveredWidgetEditGuide = *widgetGuide;
        return resolution;
    }
    for (const LayoutEditGuide& guide : layoutResolver_->layoutEditGuides_) {
        if (guide.hitRect.Contains(clientPoint)) {
            resolution.hoveredLayoutGuide = guide;
            return resolution;
        }
    }

    const auto hitAnchorTarget = [&](RenderPoint point) -> const LayoutEditAnchorRegion* {
        const LayoutEditAnchorRegion* bestRegion = nullptr;
        long long bestArea = (std::numeric_limits<long long>::max)();
        const auto scanAnchors = [&](const std::vector<LayoutEditAnchorRegion>& anchors) {
            for (auto it = anchors.rbegin(); it != anchors.rend(); ++it) {
                const LayoutEditAnchorRegion& anchor = *it;
                if (!anchor.targetRect.Contains(point)) {
                    continue;
                }
                const long long area = RectArea(anchor.targetRect);
                if (bestRegion == nullptr || area < bestArea) {
                    bestRegion = &anchor;
                    bestArea = area;
                }
            }
        };
        scanAnchors(layoutResolver_->dynamicEditableAnchorRegions_);
        scanAnchors(layoutResolver_->staticEditableAnchorRegions_);
        return bestRegion;
    };

    const auto anchorTarget = hitAnchorTarget(clientPoint);
    std::optional<LayoutEditWidgetIdentity> hoveredWidget;
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (widget.widget == nullptr || !IsWidgetHoverable(widget.widgetClass) ||
                !widget.rect.Contains(clientPoint)) {
                continue;
            }
            hoveredWidget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
            break;
        }
        if (hoveredWidget.has_value()) {
            break;
        }
    }
    if (hoveredWidget.has_value()) {
        resolution.hoveredEditableWidget = hoveredWidget;
        if (anchorTarget != nullptr && ::MatchesWidgetIdentity(anchorTarget->key.widget, *hoveredWidget)) {
            resolution.hoveredEditableAnchor = anchorTarget->key;
            return resolution;
        }
        return resolution;
    }
    if (anchorTarget != nullptr) {
        resolution.hoveredEditableAnchor = anchorTarget->key;
        return resolution;
    }
    return resolution;
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
    return metricLookupCache_.FindDefinition(config_.layout.metrics, metricRef);
}

const std::string& DashboardRenderer::ResolveConfiguredMetricSampleValueText(std::string_view metricRef) const {
    return metricLookupCache_.ResolveSampleValueText(config_.layout.metrics, metricRef);
}

const LayoutEditAnchorRegion* DashboardRenderer::FindEditableAnchorRegion(const LayoutEditAnchorKey& key) const {
    const auto findIn = [&](const std::vector<LayoutEditAnchorRegion>& regions) -> const LayoutEditAnchorRegion* {
        const auto it = std::find_if(regions.begin(), regions.end(), [&](const LayoutEditAnchorRegion& region) {
            return MatchesEditableAnchorKey(region.key, key);
        });
        return it != regions.end() ? &(*it) : nullptr;
    };
    if (const LayoutEditAnchorRegion* staticRegion = findIn(layoutResolver_->staticEditableAnchorRegions_);
        staticRegion != nullptr) {
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
            if (widget.widget == nullptr || !IsWidgetHoverable(widget.widgetClass) ||
                widget.widgetClass != *widgetClass) {
                continue;
            }
            return LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        }
    }
    return std::nullopt;
}

bool DashboardRenderer::Initialize(HWND hwnd) {
    lastError_.clear();
    presentationHwnd_ = hwnd;
    snapshotLayerValid_ = false;
    overlayLayerVisible_ = false;
    ClearReusableLayerBitmaps();
    renderer_->AttachWindow(immediatePresent_ ? hwnd : nullptr);
    renderer_->SetImmediatePresent(immediatePresent_);
    renderer_->SetHardwareLayerBitmaps(hwnd != nullptr);
    presentation_.Configure(hwnd, hwnd != nullptr && !immediatePresent_, immediatePresent_);
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
    presentation_.Shutdown();
    presentationHwnd_ = nullptr;
    snapshotLayerValid_ = false;
    overlayLayerVisible_ = false;
    ClearReusableLayerBitmaps();
    renderer_->SetHardwareLayerBitmaps(false);
    renderer_->Shutdown();
}

void DashboardRenderer::DiscardWindowRenderTarget(std::string_view reason) {
    snapshotLayerValid_ = false;
    overlayLayerVisible_ = false;
    presentation_.DiscardWindowTarget(reason);
    ClearReusableLayerBitmaps();
}

RendererStyle DashboardRenderer::BuildRendererStyle() const {
    RendererStyle style;
    style.colors = config_.layout.colors;
    style.layoutGuideSheet = config_.layout.layoutGuideSheet;
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

bool DashboardRenderer::ShouldWriteRendererTrace() const {
    return !interactiveDragTraceActive_ && trace_.Enabled(TracePrefix::Renderer);
}

void DashboardRenderer::WriteTrace(const std::string& text) const {
    if (!ShouldWriteRendererTrace()) {
        return;
    }
    trace_.Write(TracePrefix::Renderer, text);
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
    if (widget.widget == nullptr || widget.widgetClass == WidgetClass::VerticalSpring) {
        return false;
    }
    if (WidgetUsesFixedPreferredHeightInRows(widget.widgetClass)) {
        return false;
    }
    return true;
}

std::vector<const WidgetLayout*> DashboardRenderer::CollectSimilarityIndicatorWidgets(LayoutGuideAxis axis) const {
    // Size: scan the already-small result list; a separate seen-key vector measured larger.
    std::vector<const WidgetLayout*> widgets;
    for (const auto& card : layoutResolver_->resolvedLayout_.cards) {
        for (const auto& widget : card.widgets) {
            if (!SupportsLayoutSimilarityIndicator(widget) || widget.widget == nullptr) {
                continue;
            }

            const int extent = WidgetExtentForAxis(widget, axis);
            if (extent <= 0) {
                continue;
            }

            const WidgetClass widgetClass = widget.widgetClass;
            const int edgeStart = axis == LayoutGuideAxis::Vertical ? widget.rect.left : widget.rect.top;
            const int edgeEnd = axis == LayoutGuideAxis::Vertical ? widget.rect.right : widget.rect.bottom;
            const auto duplicate = [&](const WidgetLayout* candidate) {
                if (candidate == nullptr || candidate->widget == nullptr || candidate->cardId != widget.cardId ||
                    candidate->widgetClass != widgetClass || WidgetExtentForAxis(*candidate, axis) != extent) {
                    return false;
                }
                if (axis == LayoutGuideAxis::Vertical) {
                    return candidate->rect.left == edgeStart && candidate->rect.right == edgeEnd;
                }
                return candidate->rect.top == edgeStart && candidate->rect.bottom == edgeEnd;
            };
            if (std::find_if(widgets.begin(), widgets.end(), duplicate) != widgets.end()) {
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

const LayoutCardConfig* DashboardRenderer::FindCardConfigById(const std::string& id) const {
    const auto it = std::find_if(
        config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) { return card.id == id; });
    return it != config_.layout.cards.end() ? &(*it) : nullptr;
}
