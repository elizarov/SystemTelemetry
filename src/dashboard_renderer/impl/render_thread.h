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

class Trace;

struct DashboardPresentationAnimation {
    WidgetAnimationPtr animation;
    WidgetAnimationStatePtr targetState;
    RenderPoint translation{};
};

struct DashboardPresentationVersions {
    // Guards live presentation target recreation when size, DPI, or render scale changes.
    std::uint64_t surfaceVersion = 0;
    // Guards snapshot bitmap replacement and full redraw when the opaque base layer changes.
    std::uint64_t snapshotVersion = 0;
    // Guards overlay bitmap replacement and full redraw when the transparent edit/drag layer changes.
    std::uint64_t overlayVersion = 0;
    // Guards animation dirty-rectangle reuse when animation bounds or translations change.
    std::uint64_t animationGeometryVersion = 0;
    // Guards animation target retargeting and stale-key pruning when telemetry data changes.
    std::uint64_t metricVersion = 0;
};

struct DashboardPresentationFrame {
    RendererStyle style;
    RenderBitmap snapshotLayer;
    std::optional<RenderBitmap> overlayLayer;
    std::vector<DashboardPresentationAnimation> snapshotAnimations;
    std::vector<DashboardPresentationAnimation> overlayAnimations;
    DashboardPresentationVersions versions;
    int width = 0;
    int height = 0;
    bool animate = false;
    bool snapshotLayerUpdated = true;
    bool overlayLayerUpdated = true;
};

struct DashboardPresentedFrameState {
    DashboardPresentationVersions versions;
    bool hasFrame = false;
    bool hasMetricVersion = false;
    bool retainedContents = false;
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
    void SetTrace(const Trace* trace);
    void SetBitmapPool(std::shared_ptr<DashboardLayerBitmapPool> pool);
    void Shutdown();
    bool PublishFrame(DashboardPresentationFrame frame);
    bool PresentFrameSynchronously(DashboardPresentationFrame frame);
    bool PresentFrameSynchronously(Renderer& renderer, DashboardPresentationFrame frame);
    // Designed only for benchmark harnesses that repeatedly present a stored frame.
    bool PresentStoredFrameSynchronously();
    bool RenderFrameOffscreen(Renderer& renderer, DashboardPresentationFrame frame);
    void DrawFrameForCurrentTarget(Renderer& renderer, const DashboardPresentationFrame& frame) const;
    void ResetTimeline();
    void DiscardWindowTarget(std::string_view reason);
    bool HasActiveAnimations() const;
    std::string LastError() const;

private:
    bool PrepareRenderer(
        Renderer& renderer, const DashboardPresentationFrame& frame, DashboardPresentedFrameState& state);
    bool PresentFrame(Renderer& renderer,
        DashboardAnimationTimeline& timeline,
        DashboardPresentationFrame& frame,
        DashboardPresentedFrameState& presentedState);
    void DrawFrame(Renderer& renderer,
        DashboardAnimationTimeline* timeline,
        const DashboardPresentationFrame& frame,
        DashboardAnimationTimeline::Clock::time_point now) const;

    struct PreparedDirtyAnimation {
        const DashboardPresentationAnimation* command = nullptr;
        WidgetAnimationStatePtr sampledState;
        const WidgetAnimationState* drawState = nullptr;
        RenderRect dirtyRect{};
    };

    struct PreparedDirtyFrame {
        std::vector<PreparedDirtyAnimation> snapshotAnimations;
        std::vector<PreparedDirtyAnimation> overlayAnimations;
        std::vector<RenderRect> dirtyRects;
    };

    void DrawFrameDirty(Renderer& renderer,
        const DashboardPresentationFrame& frame,
        std::span<const RenderRect> dirtyRects,
        const PreparedDirtyFrame& preparedFrame) const;

    void DrawAnimations(Renderer& renderer,
        DashboardAnimationTimeline* timeline,
        const std::vector<DashboardPresentationAnimation>& animations,
        int width,
        int height,
        std::uint64_t targetVersion) const;
    PreparedDirtyFrame PrepareDirtyFrame(
        DashboardAnimationTimeline* timeline, const DashboardPresentationFrame& frame) const;
    void AppendPreparedDirtyAnimations(DashboardAnimationTimeline* timeline,
        const std::vector<DashboardPresentationAnimation>& animations,
        std::uint64_t targetVersion,
        int width,
        int height,
        std::vector<PreparedDirtyAnimation>& prepared,
        std::vector<RenderRect>& dirtyRects) const;
    void DrawPreparedDirtyAnimations(Renderer& renderer, const std::vector<PreparedDirtyAnimation>& animations) const;
    void MergeFrame(DashboardPresentationFrame& target, DashboardPresentationFrame update) const;
    void ReleaseFrameLayers(DashboardPresentationFrame frame) const;
    void ReleaseBitmap(RenderBitmap bitmap) const;
    void ThreadMain();
    void WriteTrace(std::string text) const;
    void SetLastError(std::string error);

    std::atomic<HWND> hwnd_{nullptr};
    std::atomic<const Trace*> trace_{nullptr};
    bool threaded_ = false;
    std::atomic_bool immediatePresent_{false};
    std::unique_ptr<Renderer> syncRenderer_;
    DashboardAnimationTimeline syncTimeline_;
    DashboardPresentedFrameState syncPresentedState_;
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
