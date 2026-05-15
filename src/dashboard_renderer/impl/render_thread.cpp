#include "dashboard_renderer/impl/render_thread.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace {

constexpr auto kAnimationFrameInterval = std::chrono::milliseconds(16);
constexpr std::size_t kMaxLayerBitmapPoolEntries = 8;

}  // namespace

RenderBitmap DashboardLayerBitmapPool::Acquire(int width, int height) {
    std::lock_guard lock(mutex_);
    const auto matches = [&](const RenderBitmap& bitmap) {
        return bitmap.width == width && bitmap.height == height && !bitmap.Empty();
    };
    const auto it = std::find_if(available_.begin(), available_.end(), matches);
    if (it == available_.end()) {
        return {};
    }
    RenderBitmap bitmap = std::move(*it);
    available_.erase(it);
    return bitmap;
}

void DashboardLayerBitmapPool::Release(RenderBitmap bitmap) {
    if (bitmap.Empty()) {
        return;
    }

    std::lock_guard lock(mutex_);
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
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    stopRequested_ = false;
    threaded_ = false;
    activeAnimations_.store(false);
    if (syncRenderer_ != nullptr) {
        syncRenderer_->Shutdown();
    }
    syncTimeline_.Reset();
    if (syncFrame_.has_value()) {
        ReleaseFrameLayers(std::move(*syncFrame_));
    }
    syncFrame_.reset();
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
            ReleaseFrameLayers(std::move(*pendingFrame_));
        }
        pendingFrame_ = std::move(frame);
    }
    wake_.notify_one();
    return true;
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
    const bool presented = PresentFrame(renderer, syncTimeline_, *syncFrame_, syncSurfaceVersion_);
    if (!presented) {
        SetLastError(renderer.LastError());
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
    syncTimeline_.Reset();
    activeAnimations_.store(false);
    if (threaded_) {
        {
            std::lock_guard lock(mutex_);
            resetTimelineRequested_ = true;
        }
        wake_.notify_one();
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
    if (threaded_) {
        {
            std::lock_guard lock(mutex_);
            discardTargetRequested_ = true;
            discardReason_ = std::string(reason);
        }
        wake_.notify_one();
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
    Renderer& renderer, const DashboardPresentationFrame& frame, std::uint64_t& version) {
    renderer.AttachWindow(hwnd_.load());
    renderer.SetImmediatePresent(immediatePresent_.load());
    if (version != frame.surfaceVersion) {
        renderer.DiscardWindowTarget("surface_version");
        version = frame.surfaceVersion;
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
    std::uint64_t& version) {
    if (!PrepareRenderer(renderer, frame, version)) {
        return false;
    }

    const auto now = DashboardAnimationTimeline::Clock::now();
    DashboardAnimationTimeline* activeTimeline = frame.animate ? &timeline : nullptr;
    if (activeTimeline != nullptr) {
        activeTimeline->BeginFrame(now);
    }
    const bool presented =
        renderer.DrawWindow(frame.width, frame.height, [&] { DrawFrame(renderer, activeTimeline, frame, now); });
    if (activeTimeline != nullptr) {
        activeTimeline->EndFrame();
        activeAnimations_.store(activeTimeline->HasActiveAnimations(now));
    } else {
        activeAnimations_.store(false);
    }
    if (!presented) {
        SetLastError(renderer.LastError());
    }
    return presented;
}

void DashboardRenderThread::DrawFrame(Renderer& renderer,
    DashboardAnimationTimeline* timeline,
    const DashboardPresentationFrame& frame,
    DashboardAnimationTimeline::Clock::time_point) const {
    renderer.DrawBitmap(frame.snapshotLayer, RenderPoint{0, 0});
    DrawAnimations(renderer, timeline, frame.snapshotAnimations);
    if (frame.overlayLayer.has_value()) {
        renderer.DrawBitmap(*frame.overlayLayer, RenderPoint{0, 0});
    }
    DrawAnimations(renderer, timeline, frame.overlayAnimations);
}

void DashboardRenderThread::DrawAnimations(Renderer& renderer,
    DashboardAnimationTimeline* timeline,
    const std::vector<DashboardPresentationAnimation>& animations) const {
    for (const DashboardPresentationAnimation& command : animations) {
        const WidgetAnimationPtr& animation = command.animation;
        if (animation == nullptr) {
            continue;
        }
        WidgetAnimationStatePtr target = animation->TargetState();
        if (target == nullptr) {
            continue;
        }
        WidgetAnimationStatePtr sampled =
            timeline != nullptr ? timeline->Resolve(animation->Key(), *target) : target->Clone();
        if (sampled != nullptr) {
            if (command.translation.x != 0 || command.translation.y != 0) {
                renderer.PushTranslation(command.translation);
            }
            animation->Draw(renderer, *sampled);
            if (command.translation.x != 0 || command.translation.y != 0) {
                renderer.PopTranslation();
            }
        }
    }
}

void DashboardRenderThread::MergeFrame(DashboardPresentationFrame& target, DashboardPresentationFrame update) const {
    target.style = std::move(update.style);
    target.surfaceVersion = update.surfaceVersion;
    target.metricVersion = update.metricVersion;
    target.styleVersion = update.styleVersion;
    target.width = update.width;
    target.height = update.height;
    target.animate = update.animate;

    if (update.snapshotLayerUpdated) {
        ReleaseBitmap(std::move(target.snapshotLayer));
        target.snapshotLayer = std::move(update.snapshotLayer);
        target.snapshotAnimations = std::move(update.snapshotAnimations);
        target.snapshotVersion = update.snapshotVersion;
    }
    if (update.overlayLayerUpdated) {
        if (target.overlayLayer.has_value()) {
            ReleaseBitmap(std::move(*target.overlayLayer));
        }
        target.overlayLayer = std::move(update.overlayLayer);
        target.overlayAnimations = std::move(update.overlayAnimations);
        target.overlayVersion = update.overlayVersion;
    }
    if (update.snapshotLayerUpdated || update.overlayLayerUpdated) {
        target.animationGeometryVersion = update.animationGeometryVersion;
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
        bitmapPool_->Release(std::move(bitmap));
    }
}

void DashboardRenderThread::ThreadMain() {
    std::unique_ptr<Renderer> renderer = CreateRenderer();
    DashboardAnimationTimeline timeline;
    std::uint64_t surfaceVersion = 0;
    std::optional<DashboardPresentationFrame> activeFrame;

    for (;;) {
        {
            std::unique_lock lock(mutex_);
            if (!activeFrame.has_value() && !pendingFrame_.has_value() && !stopRequested_ && !resetTimelineRequested_ &&
                !discardTargetRequested_) {
                wake_.wait(lock);
            }
            if (stopRequested_) {
                break;
            }
            if (resetTimelineRequested_) {
                timeline.Reset();
                activeAnimations_.store(false);
                resetTimelineRequested_ = false;
            }
            if (discardTargetRequested_) {
                renderer->DiscardWindowTarget(discardReason_);
                discardTargetRequested_ = false;
                discardReason_.clear();
            }
            if (pendingFrame_.has_value()) {
                if (activeFrame.has_value()) {
                    MergeFrame(*activeFrame, std::move(*pendingFrame_));
                } else {
                    activeFrame = std::move(*pendingFrame_);
                }
                pendingFrame_.reset();
            }
        }

        if (!activeFrame.has_value()) {
            continue;
        }

        const bool presented = PresentFrame(*renderer, timeline, *activeFrame, surfaceVersion);
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

        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, kAnimationFrameInterval, [&] {
            return stopRequested_ || pendingFrame_.has_value() || resetTimelineRequested_ || discardTargetRequested_;
        });
    }

    if (activeFrame.has_value()) {
        ReleaseFrameLayers(std::move(*activeFrame));
        activeFrame.reset();
    }
    renderer->Shutdown();
}

void DashboardRenderThread::SetLastError(std::string error) {
    if (error.empty()) {
        return;
    }
    std::lock_guard lock(mutex_);
    lastError_ = std::move(error);
}
