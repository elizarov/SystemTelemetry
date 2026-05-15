#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "dashboard_renderer/impl/animation_timeline.h"
#include "renderer/renderer.h"
#include "widget/animation.h"

struct DashboardPresentationFrame {
    RendererStyle style;
    RenderBitmap snapshotLayer;
    std::optional<RenderBitmap> overlayLayer;
    std::vector<WidgetAnimationPtr> snapshotAnimations;
    std::vector<WidgetAnimationPtr> overlayAnimations;
    std::uint64_t surfaceGeneration = 0;
    int width = 0;
    int height = 0;
    bool animate = false;
};

class DashboardRenderThread {
public:
    DashboardRenderThread();
    ~DashboardRenderThread();

    void Configure(HWND hwnd, bool threaded, bool immediatePresent);
    void Shutdown();
    bool PublishFrame(DashboardPresentationFrame frame);
    bool PresentFrameSynchronously(DashboardPresentationFrame frame);
    bool RenderFrameOffscreen(Renderer& renderer, DashboardPresentationFrame frame);
    void DrawFrameForCurrentTarget(Renderer& renderer, const DashboardPresentationFrame& frame) const;
    void ResetTimeline();
    void DiscardWindowTarget(std::string_view reason);
    bool HasActiveAnimations() const;
    std::string LastError() const;

private:
    bool PrepareRenderer(Renderer& renderer, const DashboardPresentationFrame& frame, std::uint64_t& generation);
    bool PresentFrame(Renderer& renderer,
        DashboardAnimationTimeline& timeline,
        DashboardPresentationFrame& frame,
        std::uint64_t& generation);
    void DrawFrame(Renderer& renderer,
        DashboardAnimationTimeline* timeline,
        const DashboardPresentationFrame& frame,
        DashboardAnimationTimeline::Clock::time_point now) const;
    void DrawAnimations(Renderer& renderer,
        DashboardAnimationTimeline* timeline,
        const std::vector<WidgetAnimationPtr>& animations) const;
    void ThreadMain();
    void SetLastError(std::string error);

    std::atomic<HWND> hwnd_{nullptr};
    bool threaded_ = false;
    std::atomic_bool immediatePresent_{false};
    std::unique_ptr<Renderer> syncRenderer_;
    DashboardAnimationTimeline syncTimeline_;
    std::uint64_t syncSurfaceGeneration_ = 0;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<DashboardPresentationFrame> pendingFrame_;
    std::thread thread_;
    bool stopRequested_ = false;
    bool resetTimelineRequested_ = false;
    bool discardTargetRequested_ = false;
    std::string discardReason_;
    std::string lastError_;
    std::atomic_bool activeAnimations_{false};
};
