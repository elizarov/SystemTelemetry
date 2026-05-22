#include "dashboard_renderer/impl/dashboard_renderer_benchmark.h"

#include <utility>

bool DashboardRendererBenchmarkAccess::BuildAnimationFrame(
    DashboardRenderer& renderer, const SystemSnapshot& snapshot, DashboardPresentationFrame& frame) {
    renderer.lastError_.clear();
    DashboardOverlayState overlayState;
    const DashboardRenderer::PresentationBuildOptions options{
        true,
        true,
    };
    const bool built = renderer.BuildPresentationFrame(snapshot, overlayState, frame, options);
    if (!built) {
        return false;
    }
    frame.animate = true;
    return true;
}

bool DashboardRendererBenchmarkAccess::BuildSnapshotHandoffFrame(
    DashboardRenderer& renderer, const SystemSnapshot& snapshot, DashboardPresentationFrame& frame) {
    renderer.lastError_.clear();
    DashboardOverlayState overlayState;
    const DashboardRenderer::PresentationBuildOptions options{
        renderer.presentationHwnd_ != nullptr,
        false,
    };
    const bool built = renderer.BuildPresentationFrame(snapshot, overlayState, frame, options);
    if (!built) {
        return false;
    }
    return true;
}

bool DashboardRendererBenchmarkAccess::PublishSnapshotHandoffFrame(
    DashboardRenderer& renderer, DashboardPresentationFrame frame) {
    renderer.lastError_.clear();
    const bool published = renderer.presentationHwnd_ == nullptr ?
        renderer.presentation_->PresentFrameSynchronously(std::move(frame)) :
        renderer.presentation_->PublishFrame(std::move(frame));
    if (!published) {
        renderer.lastError_ = renderer.presentation_->LastError();
    }
    return published && renderer.lastError_.empty();
}
