#include "dashboard_renderer/impl/render_thread.h"

#include <chrono>
#include <utility>

namespace {

constexpr auto kAnimationFrameInterval = std::chrono::milliseconds(16);

}  // namespace

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

void DashboardRenderThread::Shutdown() {
    {
        std::lock_guard lock(mutex_);
        stopRequested_ = true;
        pendingFrame_.reset();
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
}

bool DashboardRenderThread::PublishFrame(DashboardPresentationFrame frame) {
    if (!threaded_) {
        return PresentFrameSynchronously(std::move(frame));
    }
    {
        std::lock_guard lock(mutex_);
        if (stopRequested_ || !thread_.joinable()) {
            lastError_ = "renderer:render_thread_not_running";
            return false;
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
    const bool presented = PresentFrame(*syncRenderer_, syncTimeline_, frame, syncSurfaceGeneration_);
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
    Renderer& renderer, const DashboardPresentationFrame& frame, std::uint64_t& generation) {
    renderer.AttachWindow(hwnd_.load());
    renderer.SetImmediatePresent(immediatePresent_.load());
    if (generation != frame.surfaceGeneration) {
        renderer.DiscardWindowTarget("surface_generation");
        generation = frame.surfaceGeneration;
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
    std::uint64_t& generation) {
    if (!PrepareRenderer(renderer, frame, generation)) {
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

void DashboardRenderThread::DrawAnimations(
    Renderer& renderer, DashboardAnimationTimeline* timeline, const std::vector<WidgetAnimationPtr>& animations) const {
    for (const WidgetAnimationPtr& animation : animations) {
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
            animation->Draw(renderer, *sampled);
        }
    }
}

void DashboardRenderThread::ThreadMain() {
    std::unique_ptr<Renderer> renderer = CreateRenderer();
    DashboardAnimationTimeline timeline;
    std::uint64_t surfaceGeneration = 0;
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
                activeFrame = std::move(*pendingFrame_);
                pendingFrame_.reset();
            }
        }

        if (!activeFrame.has_value()) {
            continue;
        }

        const bool presented = PresentFrame(*renderer, timeline, *activeFrame, surfaceGeneration);
        if (!presented) {
            activeFrame.reset();
            continue;
        }
        if (!activeAnimations_.load()) {
            activeFrame.reset();
            continue;
        }

        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, kAnimationFrameInterval, [&] {
            return stopRequested_ || pendingFrame_.has_value() || resetTimelineRequested_ || discardTargetRequested_;
        });
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
