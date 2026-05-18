#pragma once

#include "dashboard_renderer/dashboard_renderer.h"
#include "dashboard_renderer/impl/render_thread.h"

class DashboardRendererBenchmarkAccess {
public:
    // Designed only for single-threaded benchmarks that need a reusable animated presentation frame.
    static bool BuildAnimationFrame(
        DashboardRenderer& renderer, const SystemSnapshot& snapshot, DashboardPresentationFrame& frame);
    // Designed only for benchmarks that measure snapshot layer construction before threaded handoff.
    static bool BuildSnapshotHandoffFrame(
        DashboardRenderer& renderer, const SystemSnapshot& snapshot, DashboardPresentationFrame& frame);
    // Designed only for benchmarks that publish through the live render-thread handoff.
    static bool PublishSnapshotHandoffFrame(DashboardRenderer& renderer, DashboardPresentationFrame frame);
};
