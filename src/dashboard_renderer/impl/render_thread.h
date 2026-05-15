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

struct DashboardPresentationAnimation {
    WidgetAnimationPtr animation;
    RenderPoint translation{};
};

struct DashboardPresentationFrame {
    RendererStyle style;
    RenderBitmap snapshotLayer;
    std::optional<RenderBitmap> overlayLayer;
    std::vector<DashboardPresentationAnimation> snapshotAnimations;
    std::vector<DashboardPresentationAnimation> overlayAnimations;
    std::uint64_t surfaceVersion = 0;
    std::uint64_t snapshotVersion = 0;
    std::uint64_t overlayVersion = 0;
    std::uint64_t animationGeometryVersion = 0;
    std::uint64_t metricVersion = 0;
    std::uint64_t styleVersion = 0;
    int width = 0;
    int height = 0;
    bool animate = false;
    bool snapshotLayerUpdated = true;
    bool overlayLayerUpdated = true;
};

class DashboardLayerBitmapPool {
public:
    RenderBitmap Acquire(int width, int height);
    void Release(RenderBitmap bitmap);
    void Clear();

private:
    mutable std::mutex mutex_;
    std::vector<RenderBitmap> available_;
};

class DashboardRenderThread {
public:
    DashboardRenderThread();
    ~DashboardRenderThread();

    void Configure(HWND hwnd, bool threaded, bool immediatePresent);
    void SetBitmapPool(std::shared_ptr<DashboardLayerBitmapPool> pool);
    void Shutdown();
    bool PublishFrame(DashboardPresentationFrame frame);
    bool PresentFrameSynchronously(DashboardPresentationFrame frame);
    bool PresentFrameSynchronously(Renderer& renderer, DashboardPresentationFrame frame);
#ifdef CASEDASH_BENCHMARK_TARGET
    bool PresentStoredFrameSynchronously();
#endif
    bool RenderFrameOffscreen(Renderer& renderer, DashboardPresentationFrame frame);
    void DrawFrameForCurrentTarget(Renderer& renderer, const DashboardPresentationFrame& frame) const;
    void ResetTimeline();
    void DiscardWindowTarget(std::string_view reason);
    bool HasActiveAnimations() const;
    std::string LastError() const;

private:
    bool PrepareRenderer(Renderer& renderer, const DashboardPresentationFrame& frame, std::uint64_t& version);
    bool PresentFrame(Renderer& renderer,
        DashboardAnimationTimeline& timeline,
        DashboardPresentationFrame& frame,
        std::uint64_t& version);
    void DrawFrame(Renderer& renderer,
        DashboardAnimationTimeline* timeline,
        const DashboardPresentationFrame& frame,
        DashboardAnimationTimeline::Clock::time_point now) const;
    void DrawAnimations(Renderer& renderer,
        DashboardAnimationTimeline* timeline,
        const std::vector<DashboardPresentationAnimation>& animations) const;
    void MergeFrame(DashboardPresentationFrame& target, DashboardPresentationFrame update) const;
    void ReleaseFrameLayers(DashboardPresentationFrame frame) const;
    void ReleaseBitmap(RenderBitmap bitmap) const;
    void ThreadMain();
    void SetLastError(std::string error);

    std::atomic<HWND> hwnd_{nullptr};
    bool threaded_ = false;
    std::atomic_bool immediatePresent_{false};
    std::unique_ptr<Renderer> syncRenderer_;
    DashboardAnimationTimeline syncTimeline_;
    std::uint64_t syncSurfaceVersion_ = 0;
    std::optional<DashboardPresentationFrame> syncFrame_;
    std::shared_ptr<DashboardLayerBitmapPool> bitmapPool_;

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
