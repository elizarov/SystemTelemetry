#include "dashboard_renderer/impl/render_thread.h"

#include <algorithm>
#include <utility>

#include "util/high_precision_timer.h"
#include "util/trace.h"

namespace {

constexpr std::size_t kMaxLayerBitmapPoolEntries = 8;
constexpr int kAnimationDirtyPadding = 3;

RenderRect OffsetRect(RenderRect rect, RenderPoint offset) {
    rect.left += offset.x;
    rect.right += offset.x;
    rect.top += offset.y;
    rect.bottom += offset.y;
    return rect;
}

RenderRect ClipRectToSurface(RenderRect rect, int width, int height) {
    rect.left = std::clamp(rect.left, 0, width);
    rect.right = std::clamp(rect.right, 0, width);
    rect.top = std::clamp(rect.top, 0, height);
    rect.bottom = std::clamp(rect.bottom, 0, height);
    return rect;
}

RenderRect AnimationClipBounds(const WidgetAnimation& animation, RenderPoint translation, int width, int height) {
    return ClipRectToSurface(
        OffsetRect(animation.DirtyBounds(), translation).Inflate(kAnimationDirtyPadding, kAnimationDirtyPadding),
        width,
        height);
}

const char* BoolText(bool value) {
    return value ? "yes" : "no";
}

const char* TrackRetentionText(DashboardAnimationTimeline::TrackRetention retention) {
    switch (retention) {
        case DashboardAnimationTimeline::TrackRetention::PruneUntouched:
            return "prune_untouched";
        case DashboardAnimationTimeline::TrackRetention::KeepUntouched:
            return "keep_untouched";
    }
    return "unknown";
}

void RecordAnimationFrameTiming(const Trace* trace, HighPrecisionTimer::Tick startedAt) {
    if (trace == nullptr || startedAt == 0) {
        return;
    }
    trace->Timings().Record(
        *trace, "animation_frame", HighPrecisionTimer::Elapsed(startedAt, HighPrecisionTimer::Now()));
}

}  // namespace

void DashboardLayerBitmapPool::SetLiveLayerSize(int width, int height) {
    std::lock_guard lock(mutex_);
    width = std::max(0, width);
    height = std::max(0, height);
    if (liveLayerWidth_ == width && liveLayerHeight_ == height) {
        return;
    }
    liveLayerWidth_ = width;
    liveLayerHeight_ = height;
    available_.clear();
}

RenderBitmap DashboardLayerBitmapPool::AcquireLiveLayerBitmap(int width, int height) {
    std::lock_guard lock(mutex_);
    if (width != liveLayerWidth_ || height != liveLayerHeight_) {
        return {};
    }
    const auto matches = [&](const RenderBitmap& bitmap) {
        return bitmap.width == width && bitmap.height == height && bitmap.IsLiveLayer();
    };
    const auto it = std::find_if(available_.begin(), available_.end(), matches);
    if (it == available_.end()) {
        return {};
    }
    RenderBitmap bitmap = std::move(*it);
    available_.erase(it);
    return bitmap;
}

void DashboardLayerBitmapPool::ReleaseLiveLayerBitmap(RenderBitmap bitmap) {
    if (!bitmap.IsLiveLayer()) {
        return;
    }

    std::lock_guard lock(mutex_);
    if (bitmap.width != liveLayerWidth_ || bitmap.height != liveLayerHeight_) {
        return;
    }
    if (available_.size() >= kMaxLayerBitmapPoolEntries) {
        return;
    }
    available_.push_back(std::move(bitmap));
}

void DashboardLayerBitmapPool::Clear() {
    std::lock_guard lock(mutex_);
    available_.clear();
}

DashboardRenderThread::DashboardRenderThread() = default;

DashboardRenderThread::~DashboardRenderThread() {
    Shutdown();
}

void DashboardRenderThread::Configure(HWND hwnd, bool threaded, bool immediatePresent) {
    if (threaded_ != threaded) {
        WriteTrace("render_thread_shutdown_request reason=configure_threading_change old_threaded=" +
                   std::string(BoolText(threaded_)) + " new_threaded=" + BoolText(threaded));
        Shutdown();
    }

    hwnd_.store(hwnd);
    threaded_ = threaded;
    immediatePresent_.store(immediatePresent);
    if (threaded_) {
        if (!thread_.joinable()) {
            stopRequested_ = false;
            thread_ = std::thread(&DashboardRenderThread::ThreadMain, this);
        }
        return;
    }

    if (syncRenderer_ == nullptr) {
        syncRenderer_ = CreateRenderer();
    }
    syncRenderer_->AttachWindow(hwnd_.load());
    syncRenderer_->SetImmediatePresent(immediatePresent_.load());
}

void DashboardRenderThread::SetTrace(const Trace* trace) {
    trace_.store(trace);
}

void DashboardRenderThread::SetBitmapPool(std::shared_ptr<DashboardLayerBitmapPool> pool) {
    std::lock_guard lock(mutex_);
    bitmapPool_ = std::move(pool);
}

void DashboardRenderThread::Shutdown() {
    {
        std::lock_guard lock(mutex_);
        stopRequested_ = true;
        if (pendingFrame_.has_value()) {
            ReleaseFrameLayers(std::move(*pendingFrame_));
            pendingFrame_.reset();
        }
        if (pendingFrameRequestId_ != 0) {
            completedFrameRequestId_ = std::max(completedFrameRequestId_, pendingFrameRequestId_);
            completedFrameRequestSucceeded_ = false;
            pendingFrameRequestId_ = 0;
        }
    }
    wake_.notify_all();
    framePresented_.notify_all();
    discardCompleted_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    stopRequested_ = false;
    threaded_ = false;
    activeAnimations_.store(false);
    if (syncRenderer_ != nullptr) {
        syncRenderer_->Shutdown();
    }
    WriteTrace(
        "animation_timeline_reset owner=sync reason=shutdown tracks=" + std::to_string(syncTimeline_.TrackCount()));
    syncTimeline_.Reset();
    if (syncFrame_.has_value()) {
        ReleaseFrameLayers(std::move(*syncFrame_));
    }
    syncFrame_.reset();
    syncPresentedState_ = {};
}

bool DashboardRenderThread::PublishFrame(DashboardPresentationFrame frame) {
    if (!threaded_) {
        return PresentFrameSynchronously(std::move(frame));
    }
    {
        std::lock_guard lock(mutex_);
        if (stopRequested_ || !thread_.joinable()) {
            lastError_ = "renderer:render_thread_not_running";
            ReleaseFrameLayers(std::move(frame));
            return false;
        }
        if (pendingFrame_.has_value()) {
            CoalescePendingFrame(*pendingFrame_, std::move(frame));
        } else {
            pendingFrame_ = std::move(frame);
        }
    }
    wake_.notify_one();
    return true;
}

bool DashboardRenderThread::PublishFrameAndWait(DashboardPresentationFrame frame) {
    if (!threaded_) {
        return PresentFrameSynchronously(std::move(frame));
    }

    std::uint64_t requestId = 0;
    {
        std::lock_guard lock(mutex_);
        if (stopRequested_ || !thread_.joinable()) {
            lastError_ = "renderer:render_thread_not_running";
            ReleaseFrameLayers(std::move(frame));
            return false;
        }
        requestId = ++nextFrameRequestId_;
        if (pendingFrame_.has_value()) {
            CoalescePendingFrame(*pendingFrame_, std::move(frame));
        } else {
            pendingFrame_ = std::move(frame);
        }
        pendingFrameRequestId_ = std::max(pendingFrameRequestId_, requestId);
    }
    wake_.notify_one();

    std::unique_lock lock(mutex_);
    framePresented_.wait(lock, [&] { return stopRequested_ || completedFrameRequestId_ >= requestId; });
    return completedFrameRequestId_ >= requestId && completedFrameRequestSucceeded_;
}

bool DashboardRenderThread::PresentFrameSynchronously(DashboardPresentationFrame frame) {
    if (syncRenderer_ == nullptr) {
        syncRenderer_ = CreateRenderer();
        syncRenderer_->AttachWindow(hwnd_.load());
        syncRenderer_->SetImmediatePresent(immediatePresent_.load());
    }
    return PresentFrameSynchronously(*syncRenderer_, std::move(frame));
}

bool DashboardRenderThread::PresentFrameSynchronously(Renderer& renderer, DashboardPresentationFrame frame) {
    if (syncFrame_.has_value()) {
        MergeFrame(*syncFrame_, std::move(frame));
    } else {
        syncFrame_ = std::move(frame);
    }
    const bool presented = PresentFrame(renderer, syncTimeline_, *syncFrame_, syncPresentedState_);
    if (!presented) {
        SetLastError(renderer.LastError());
    }
    return presented;
}

bool DashboardRenderThread::PresentStoredFrameSynchronously() {
    if (syncRenderer_ == nullptr || !syncFrame_.has_value()) {
        SetLastError("renderer:no_stored_frame");
        return false;
    }
    const bool presented = PresentFrame(*syncRenderer_, syncTimeline_, *syncFrame_, syncPresentedState_);
    if (!presented) {
        SetLastError(syncRenderer_->LastError());
    }
    return presented;
}

bool DashboardRenderThread::RenderFrameOffscreen(Renderer& renderer, DashboardPresentationFrame frame) {
    if (!renderer.SetStyle(frame.style)) {
        SetLastError(renderer.LastError());
        return false;
    }
    const bool rendered =
        renderer.DrawOffscreen(frame.width, frame.height, [&] { DrawFrameForCurrentTarget(renderer, frame); });
    if (!rendered) {
        SetLastError(renderer.LastError());
    }
    return rendered;
}

void DashboardRenderThread::DrawFrameForCurrentTarget(
    Renderer& renderer, const DashboardPresentationFrame& frame) const {
    DrawFrame(renderer, nullptr, frame, DashboardAnimationTimeline::Clock::now());
}

void DashboardRenderThread::ResetTimeline() {
    WriteTrace("animation_timeline_reset owner=sync reason=explicit_request tracks=" +
               std::to_string(syncTimeline_.TrackCount()));
    syncTimeline_.Reset();
    activeAnimations_.store(false);
    if (threaded_) {
        {
            std::lock_guard lock(mutex_);
            resetTimelineRequested_ = true;
        }
        WriteTrace("animation_timeline_reset_request owner=thread reason=explicit_request");
        wake_.notify_one();
    }
}

void DashboardRenderThread::SetAnimationPresentationSuspended(bool suspended) {
    animationPresentationSuspended_.store(suspended);
    if (!suspended) {
        wake_.notify_all();
    }
}

void DashboardRenderThread::DiscardWindowTarget(std::string_view reason) {
    if (syncRenderer_ != nullptr) {
        syncRenderer_->DiscardWindowTarget(reason);
    }
    if (syncFrame_.has_value()) {
        ReleaseFrameLayers(std::move(*syncFrame_));
    }
    syncFrame_.reset();
    const std::uint64_t metricVersion = syncPresentedState_.versions.metricVersion;
    const bool hasMetricVersion = syncPresentedState_.hasMetricVersion;
    syncPresentedState_ = {};
    syncPresentedState_.versions.metricVersion = metricVersion;
    syncPresentedState_.hasMetricVersion = hasMetricVersion;
    if (threaded_) {
        std::uint64_t requestId = 0;
        {
            std::lock_guard lock(mutex_);
            if (pendingFrame_.has_value()) {
                ReleaseFrameLayers(std::move(*pendingFrame_));
                pendingFrame_.reset();
            }
            if (pendingFrameRequestId_ != 0) {
                completedFrameRequestId_ = std::max(completedFrameRequestId_, pendingFrameRequestId_);
                completedFrameRequestSucceeded_ = false;
                pendingFrameRequestId_ = 0;
                framePresented_.notify_all();
            }
            discardTargetRequested_ = true;
            discardReason_ = std::string(reason);
            requestId = ++discardRequestId_;
        }
        wake_.notify_one();
        std::unique_lock lock(mutex_);
        discardCompleted_.wait(lock, [&] { return stopRequested_ || completedDiscardRequestId_ >= requestId; });
    }
}

bool DashboardRenderThread::HasActiveAnimations() const {
    return activeAnimations_.load();
}

std::string DashboardRenderThread::LastError() const {
    std::lock_guard lock(mutex_);
    return lastError_;
}

bool DashboardRenderThread::PrepareRenderer(
    Renderer& renderer, const DashboardPresentationFrame& frame, DashboardPresentedFrameState& state) {
    renderer.AttachWindow(hwnd_.load());
    renderer.SetImmediatePresent(immediatePresent_.load());
    if (state.versions.surfaceVersion != frame.versions.surfaceVersion) {
        const std::uint64_t metricVersion = state.versions.metricVersion;
        const bool hasMetricVersion = state.hasMetricVersion;
        renderer.DiscardWindowTarget("surface_version");
        state = {};
        state.versions.surfaceVersion = frame.versions.surfaceVersion;
        state.versions.metricVersion = metricVersion;
        state.hasMetricVersion = hasMetricVersion;
    }
    if (!renderer.SetStyle(frame.style)) {
        SetLastError(renderer.LastError());
        return false;
    }
    return true;
}

bool DashboardRenderThread::PresentFrame(Renderer& renderer,
    DashboardAnimationTimeline& timeline,
    DashboardPresentationFrame& frame,
    DashboardPresentedFrameState& presentedState) {
    const Trace* trace = trace_.load();
    const bool metricTargetsUpdated =
        !presentedState.hasMetricVersion || presentedState.versions.metricVersion != frame.versions.metricVersion;
    if (!PrepareRenderer(renderer, frame, presentedState)) {
        return false;
    }

    const auto now = DashboardAnimationTimeline::Clock::now();
    DashboardAnimationTimeline* activeTimeline = frame.animate ? &timeline : nullptr;
    if (activeTimeline != nullptr) {
        activeTimeline->BeginFrame(now);
    }
    const bool fullRedraw = !frame.animate || !presentedState.hasFrame ||
                            presentedState.versions.snapshotVersion != frame.versions.snapshotVersion ||
                            presentedState.versions.overlayVersion != frame.versions.overlayVersion ||
                            presentedState.versions.animationGeometryVersion != frame.versions.animationGeometryVersion;
    bool presented = true;
    bool retainedContents = presentedState.retainedContents;
    if (fullRedraw) {
        const auto drawFullFrame = [&] {
            const HighPrecisionTimer::Tick animationStartedAt = HighPrecisionTimer::Now();
            DrawFrame(renderer, activeTimeline, frame, now);
            if (activeTimeline != nullptr) {
                RecordAnimationFrameTiming(trace, animationStartedAt);
            }
        };
        if (frame.animate) {
            presented = renderer.DrawWindowRetained(frame.width, frame.height, drawFullFrame);
            retainedContents = true;
        } else {
            presented = renderer.DrawWindow(frame.width, frame.height, drawFullFrame);
            retainedContents = false;
        }
    } else {
        const HighPrecisionTimer::Tick animationStartedAt = HighPrecisionTimer::Now();
        const PreparedDirtyFrame preparedFrame = PrepareDirtyFrame(activeTimeline, frame);
        if (!preparedFrame.dirtyRects.empty()) {
            const RenderRect fullSurface{0, 0, frame.width, frame.height};
            const std::span<const RenderRect> redrawRects =
                retainedContents
                    ? std::span<const RenderRect>(preparedFrame.dirtyRects.data(), preparedFrame.dirtyRects.size())
                    : std::span<const RenderRect>(&fullSurface, 1);
            presented = renderer.DrawWindowDirty(frame.width, frame.height, redrawRects, [&](auto dirtyRects) {
                DrawFrameDirty(renderer, frame, dirtyRects, preparedFrame);
                RecordAnimationFrameTiming(trace, animationStartedAt);
            });
            retainedContents = true;
        }
    }
    if (activeTimeline != nullptr) {
        const auto retention = metricTargetsUpdated ? DashboardAnimationTimeline::TrackRetention::PruneUntouched
                                                    : DashboardAnimationTimeline::TrackRetention::KeepUntouched;
        const std::size_t trackCountBeforeEndFrame = activeTimeline->TrackCount();
        const std::size_t prunedCount = activeTimeline->EndFrame(retention);
        const std::size_t trackCountAfterEndFrame = activeTimeline->TrackCount();
        if (prunedCount > 0) {
            WriteTrace("animation_timeline_prune retention=" + std::string(TrackRetentionText(retention)) + " pruned=" +
                       std::to_string(prunedCount) + " before=" + std::to_string(trackCountBeforeEndFrame) +
                       " after=" + std::to_string(trackCountAfterEndFrame) +
                       " metric_version=" + std::to_string(frame.versions.metricVersion) +
                       " previous_metric_version=" + std::to_string(presentedState.versions.metricVersion) +
                       " had_previous_metric=" + BoolText(presentedState.hasMetricVersion) +
                       " surface_version=" + std::to_string(frame.versions.surfaceVersion) +
                       " snapshot_version=" + std::to_string(frame.versions.snapshotVersion) +
                       " overlay_version=" + std::to_string(frame.versions.overlayVersion) +
                       " animation_geometry_version=" + std::to_string(frame.versions.animationGeometryVersion));
        }
        activeAnimations_.store(activeTimeline->HasActiveAnimations(now));
    } else {
        activeAnimations_.store(false);
    }
    if (!presented) {
        SetLastError(renderer.LastError());
    } else {
        presentedState.versions = frame.versions;
        presentedState.hasFrame = true;
        presentedState.hasMetricVersion = true;
        presentedState.retainedContents = retainedContents;
    }
    return presented;
}

void DashboardRenderThread::DrawFrame(Renderer& renderer,
    DashboardAnimationTimeline* timeline,
    const DashboardPresentationFrame& frame,
    DashboardAnimationTimeline::Clock::time_point) const {
    renderer.DrawBitmap(frame.snapshotLayer, RenderPoint{0, 0});
    DrawAnimations(
        renderer, timeline, frame.snapshotAnimations, frame.width, frame.height, frame.versions.metricVersion);
    if (frame.overlayLayer.has_value()) {
        renderer.DrawBitmap(*frame.overlayLayer, RenderPoint{0, 0});
    }
    DrawAnimations(
        renderer, timeline, frame.overlayAnimations, frame.width, frame.height, frame.versions.metricVersion);
}

void DashboardRenderThread::DrawFrameDirty(Renderer& renderer,
    const DashboardPresentationFrame& frame,
    std::span<const RenderRect> dirtyRects,
    const PreparedDirtyFrame& preparedFrame) const {
    renderer.DrawBitmapRegions(frame.snapshotLayer, dirtyRects);
    DrawPreparedDirtyAnimations(renderer, preparedFrame.snapshotAnimations);
    if (frame.overlayLayer.has_value()) {
        renderer.DrawBitmapRegions(*frame.overlayLayer, dirtyRects);
    }
    DrawPreparedDirtyAnimations(renderer, preparedFrame.overlayAnimations);
}

void DashboardRenderThread::DrawAnimations(Renderer& renderer,
    DashboardAnimationTimeline* timeline,
    const std::vector<DashboardPresentationAnimation>& animations,
    int width,
    int height,
    std::uint64_t targetVersion) const {
    for (const DashboardPresentationAnimation& command : animations) {
        const WidgetAnimationPtr& animation = command.animation;
        if (animation == nullptr || command.targetState == nullptr) {
            continue;
        }
        const RenderRect clipRect = AnimationClipBounds(*animation, command.translation, width, height);
        if (clipRect.IsEmpty()) {
            continue;
        }
        WidgetAnimationStatePtr sampled;
        const WidgetAnimationState* drawState = command.targetState.get();
        if (timeline != nullptr) {
            sampled = timeline->Resolve(animation->Key(), *command.targetState, targetVersion);
            drawState = sampled.get();
        }
        if (drawState != nullptr) {
            renderer.PushClipRect(clipRect);
            if (command.translation.x != 0 || command.translation.y != 0) {
                renderer.PushTranslation(command.translation);
            }
            animation->Draw(renderer, *drawState);
            if (command.translation.x != 0 || command.translation.y != 0) {
                renderer.PopTranslation();
            }
            renderer.PopClipRect();
        }
    }
}

DashboardRenderThread::PreparedDirtyFrame DashboardRenderThread::PrepareDirtyFrame(
    DashboardAnimationTimeline* timeline, const DashboardPresentationFrame& frame) const {
    PreparedDirtyFrame preparedFrame;
    preparedFrame.snapshotAnimations.reserve(frame.snapshotAnimations.size());
    preparedFrame.overlayAnimations.reserve(frame.overlayAnimations.size());
    preparedFrame.dirtyRects.reserve(frame.snapshotAnimations.size() + frame.overlayAnimations.size());
    AppendPreparedDirtyAnimations(timeline,
        frame.snapshotAnimations,
        frame.versions.metricVersion,
        frame.width,
        frame.height,
        preparedFrame.snapshotAnimations,
        preparedFrame.dirtyRects);
    AppendPreparedDirtyAnimations(timeline,
        frame.overlayAnimations,
        frame.versions.metricVersion,
        frame.width,
        frame.height,
        preparedFrame.overlayAnimations,
        preparedFrame.dirtyRects);
    return preparedFrame;
}

void DashboardRenderThread::AppendPreparedDirtyAnimations(DashboardAnimationTimeline* timeline,
    const std::vector<DashboardPresentationAnimation>& animations,
    std::uint64_t targetVersion,
    int width,
    int height,
    std::vector<PreparedDirtyAnimation>& prepared,
    std::vector<RenderRect>& dirtyRects) const {
    for (const DashboardPresentationAnimation& command : animations) {
        const WidgetAnimationPtr& animation = command.animation;
        if (animation == nullptr || command.targetState == nullptr) {
            continue;
        }
        RenderRect bounds = AnimationClipBounds(*animation, command.translation, width, height);
        if (bounds.IsEmpty()) {
            continue;
        }
        PreparedDirtyAnimation item;
        item.command = &command;
        item.dirtyRect = bounds;
        item.drawState = command.targetState.get();
        if (timeline != nullptr) {
            item.sampledState = timeline->Resolve(animation->Key(), *command.targetState, targetVersion);
            item.drawState = item.sampledState.get();
        }
        if (item.drawState != nullptr) {
            dirtyRects.push_back(bounds);
            prepared.push_back(std::move(item));
        }
    }
}

void DashboardRenderThread::DrawPreparedDirtyAnimations(
    Renderer& renderer, const std::vector<PreparedDirtyAnimation>& animations) const {
    for (const PreparedDirtyAnimation& item : animations) {
        const DashboardPresentationAnimation& command = *item.command;
        renderer.PushClipRect(item.dirtyRect);
        if (command.translation.x != 0 || command.translation.y != 0) {
            renderer.PushTranslation(command.translation);
        }
        command.animation->Draw(renderer, *item.drawState);
        if (command.translation.x != 0 || command.translation.y != 0) {
            renderer.PopTranslation();
        }
        renderer.PopClipRect();
    }
}

void DashboardRenderThread::CoalescePendingFrame(
    DashboardPresentationFrame& target, DashboardPresentationFrame update) const {
    target.style = std::move(update.style);
    target.versions.surfaceVersion = update.versions.surfaceVersion;
    target.versions.metricVersion = update.versions.metricVersion;
    target.width = update.width;
    target.height = update.height;
    target.animate = update.animate;

    if (update.snapshotLayerUpdated) {
        if (target.snapshotLayerUpdated) {
            ReleaseBitmap(std::move(target.snapshotLayer));
        }
        target.snapshotLayer = std::move(update.snapshotLayer);
        target.snapshotAnimations = std::move(update.snapshotAnimations);
        target.versions.snapshotVersion = update.versions.snapshotVersion;
        target.snapshotLayerUpdated = true;
    }
    if (update.overlayLayerUpdated) {
        if (target.overlayLayer.has_value()) {
            ReleaseBitmap(std::move(*target.overlayLayer));
        }
        target.overlayLayer = std::move(update.overlayLayer);
        target.overlayAnimations = std::move(update.overlayAnimations);
        target.versions.overlayVersion = update.versions.overlayVersion;
        target.overlayLayerUpdated = true;
    }
    if (update.snapshotLayerUpdated || update.overlayLayerUpdated) {
        target.versions.animationGeometryVersion = update.versions.animationGeometryVersion;
    }
}

void DashboardRenderThread::MergeFrame(DashboardPresentationFrame& target, DashboardPresentationFrame update) const {
    target.style = std::move(update.style);
    target.versions.surfaceVersion = update.versions.surfaceVersion;
    target.versions.metricVersion = update.versions.metricVersion;
    target.width = update.width;
    target.height = update.height;
    target.animate = update.animate;

    if (update.snapshotLayerUpdated) {
        ReleaseBitmap(std::move(target.snapshotLayer));
        target.snapshotLayer = std::move(update.snapshotLayer);
        target.snapshotAnimations = std::move(update.snapshotAnimations);
        target.versions.snapshotVersion = update.versions.snapshotVersion;
    }
    if (update.overlayLayerUpdated) {
        if (target.overlayLayer.has_value()) {
            ReleaseBitmap(std::move(*target.overlayLayer));
        }
        target.overlayLayer = std::move(update.overlayLayer);
        target.overlayAnimations = std::move(update.overlayAnimations);
        target.versions.overlayVersion = update.versions.overlayVersion;
    }
    if (update.snapshotLayerUpdated || update.overlayLayerUpdated) {
        target.versions.animationGeometryVersion = update.versions.animationGeometryVersion;
    }
    target.snapshotLayerUpdated = true;
    target.overlayLayerUpdated = true;
}

void DashboardRenderThread::ReleaseFrameLayers(DashboardPresentationFrame frame) const {
    ReleaseBitmap(std::move(frame.snapshotLayer));
    if (frame.overlayLayer.has_value()) {
        ReleaseBitmap(std::move(*frame.overlayLayer));
    }
}

void DashboardRenderThread::ReleaseBitmap(RenderBitmap bitmap) const {
    if (bitmapPool_ != nullptr) {
        bitmapPool_->ReleaseLiveLayerBitmap(std::move(bitmap));
    }
}

void DashboardRenderThread::ThreadMain() {
    std::unique_ptr<Renderer> renderer = CreateRenderer();
    DashboardAnimationTimeline timeline;
    DashboardPresentedFrameState presentedState;
    std::optional<DashboardPresentationFrame> activeFrame;
    std::uint64_t activeFrameRequestId = 0;

    for (;;) {
        {
            std::unique_lock lock(mutex_);
            if (!activeFrame.has_value() && !pendingFrame_.has_value() && !stopRequested_ && !resetTimelineRequested_ &&
                !discardTargetRequested_) {
                wake_.wait(lock);
            }
            if (animationPresentationSuspended_.load() && activeFrame.has_value() && !pendingFrame_.has_value() &&
                !stopRequested_ && !resetTimelineRequested_ && !discardTargetRequested_) {
                wake_.wait(lock, [&] {
                    return stopRequested_ || pendingFrame_.has_value() || resetTimelineRequested_ ||
                           discardTargetRequested_ || !animationPresentationSuspended_.load();
                });
            }
            if (stopRequested_) {
                break;
            }
            if (resetTimelineRequested_) {
                WriteTrace("animation_timeline_reset owner=thread reason=explicit_request tracks=" +
                           std::to_string(timeline.TrackCount()));
                timeline.Reset();
                activeAnimations_.store(false);
                resetTimelineRequested_ = false;
            }
            if (discardTargetRequested_) {
                renderer->DiscardWindowTarget(discardReason_);
                if (activeFrame.has_value()) {
                    ReleaseFrameLayers(std::move(*activeFrame));
                    activeFrame.reset();
                }
                if (activeFrameRequestId != 0) {
                    completedFrameRequestId_ = std::max(completedFrameRequestId_, activeFrameRequestId);
                    completedFrameRequestSucceeded_ = false;
                    framePresented_.notify_all();
                }
                activeFrameRequestId = 0;
                const std::uint64_t metricVersion = presentedState.versions.metricVersion;
                const bool hasMetricVersion = presentedState.hasMetricVersion;
                presentedState = {};
                presentedState.versions.metricVersion = metricVersion;
                presentedState.hasMetricVersion = hasMetricVersion;
                discardTargetRequested_ = false;
                discardReason_.clear();
                completedDiscardRequestId_ = discardRequestId_;
                discardCompleted_.notify_all();
            }
            if (pendingFrame_.has_value()) {
                if (activeFrame.has_value()) {
                    MergeFrame(*activeFrame, std::move(*pendingFrame_));
                } else {
                    activeFrame = std::move(*pendingFrame_);
                }
                activeFrameRequestId = std::max(activeFrameRequestId, pendingFrameRequestId_);
                pendingFrame_.reset();
                pendingFrameRequestId_ = 0;
            }
        }

        if (!activeFrame.has_value()) {
            continue;
        }

        const bool presented = PresentFrame(*renderer, timeline, *activeFrame, presentedState);
        if (activeFrameRequestId != 0) {
            {
                std::lock_guard lock(mutex_);
                completedFrameRequestId_ = std::max(completedFrameRequestId_, activeFrameRequestId);
                completedFrameRequestSucceeded_ = presented;
            }
            framePresented_.notify_all();
            activeFrameRequestId = 0;
        }
        if (!presented) {
            ReleaseFrameLayers(std::move(*activeFrame));
            activeFrame.reset();
            continue;
        }
        if (!activeAnimations_.load()) {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] {
                return stopRequested_ || pendingFrame_.has_value() || resetTimelineRequested_ ||
                       discardTargetRequested_;
            });
            continue;
        }
        // Animation cadence comes from the vsynced presentation backend; the render thread must not add a timer.
    }

    if (activeFrame.has_value()) {
        ReleaseFrameLayers(std::move(*activeFrame));
        activeFrame.reset();
    }
    WriteTrace("animation_timeline_reset owner=thread reason=shutdown tracks=" + std::to_string(timeline.TrackCount()));
    timeline.Reset();
    renderer->Shutdown();
}

void DashboardRenderThread::WriteTrace(std::string text) const {
    const Trace* trace = trace_.load();
    if (trace == nullptr || !trace->Enabled(TracePrefix::Renderer)) {
        return;
    }
    trace->Write(TracePrefix::Renderer, text);
}

void DashboardRenderThread::SetLastError(std::string error) {
    if (error.empty()) {
        return;
    }
    std::lock_guard lock(mutex_);
    lastError_ = std::move(error);
}
