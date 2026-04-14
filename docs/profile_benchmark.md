# Profile Benchmark Log

## Purpose

This file records the current layout-edit drag benchmark baseline, the latest confirmed hotspots, and the optimization hypotheses that have already been tested. Keep it current after benchmark or profiling work so future experiments can build on prior results instead of repeating failed ideas.

## Benchmark Workflow

- Start the elevated daemon once with `profile_benchmark.cmd /daemon-start` when repeated unattended profiling runs are needed.
- Measure the repeatable benchmark with `build\SystemTelemetryBenchmarks.exe 240 2`.
- Capture a full profile with `profile_benchmark.cmd 240 2` when a change materially moves the benchmark or when hotspot confirmation is needed.
- `profile_benchmark.cmd` rebuilds automatically through the daemon path, so profiling runs do not need a separate preceding `build.cmd` step.
- Treat `build\SystemTelemetryBenchmarks.exe 240 2` as the fast comparison loop and the WPR profile as hotspot validation.

## Current Known Baseline

- Original baseline before the drag-path optimizations in this workstream:
  - `drag_loop per_iter_ms=7.24`
  - `snap avg_ms=2.34`
  - `paint_draw avg_ms=3.96`
- Best measured result reached during this workstream:
  - `drag_loop per_iter_ms=4.43`
  - `snap avg_ms=0.20`
  - `paint_draw avg_ms=3.73`
- Current repeatable result on the optimized tree:
  - `drag_loop per_iter_ms=4.43` to `5.21`
  - `snap avg_ms=0.19` to `0.21`
  - `apply avg_ms=0.38` to `0.45`
  - `paint_draw avg_ms=3.73` to `4.37`

## Current Confirmed Hotspots

Confirmed inclusive hotspots from the latest useful WPR capture in this workstream:

- `DashboardRenderer::Draw` about `60%`
- `ThroughputWidget::Draw` about `18%`
- ``anonymous namespace'::DrawGraph`` about `18%`
- `GaugeWidget::Draw` about `14%`
- `GdiPlus.dll` still accounts for about `21%` of inclusive module hits, while the coarse function table only surfaces minor named leaves such as `DashboardRenderer::DrawPanel` and `DashboardRenderer::DrawAlphaCapsule` beyond the main widget hot path.

Interpretation:

- Snap-path work is no longer the main limiter after the latest preview-resolve optimization.
- The remaining wall time is now dominated by real paint work, especially throughput graph drawing and gauge fills, while text-anchor bookkeeping still benefits from staying on the cheapest available path.
- Fresh rebuilds and profile runs on this machine still show meaningful variance, so treat the best reruns as opportunity signals and the wider range above as the safer working baseline.
- `DashboardLayoutResolver::ResolveLayout` no longer appears among the top benchmark user-mode functions in the latest useful capture, which matches the large drop in `snap avg_ms`.

## Kept Optimizations

These changes produced real wins and remain in the codebase:

- Avoid full config copies during snap evaluation by applying preview weights directly in the renderer and resolving layout from there.
- Group snap candidates by widget so one snap search can serve multiple target extents with shared extent evaluation.
- Refactor layout similarity indicator collection to avoid repeated representative scans and reduce per-frame container churn.
- Build only the one live gauge usage-fill path that the current metric needs instead of prebuilding every cumulative gauge fill path during each relayout.
- Resolve snap-preview guide probes through an extent-only layout pass that skips widget instantiation, widget layout-state caching, and edit-artifact rebuilds.
- Reuse draw-time text layout results for dynamic text anchors and measure static text anchors through one shared HDC during static-anchor rebuilds.
- Reuse one cached `DashboardMetricSource` across successive paints while the resolved `SystemSnapshot` revision stays unchanged, so drag frames reuse smoothed throughput history and formatted metric payloads until telemetry publishes a newer snapshot.
- Fix the title-hover regression introduced during optimization work so card title text highlights correctly again.

## Tested Hypotheses

### Hypothesis: Avoid full config copies during snap probing

- Change:
  - Add renderer-side guide-weight preview application and evaluate widget extents from the previewed renderer instead of cloning full config state for each snap probe.
- Result:
  - Helped.
- Observed effect:
  - `snap avg_ms` dropped from about `2.34` to about `1.43`.
- Conclusion:
  - Snap evaluation had avoidable config churn and this change was worth keeping.

### Hypothesis: Group snap candidates by widget identity

- Change:
  - Reuse one nearest-snap search for multiple candidate target extents belonging to the same widget.
- Result:
  - Helped.
- Observed effect:
  - Reduced repeated extent-evaluation work in the snap path and contributed to the improved `snap avg_ms`.
- Conclusion:
  - Shared snap search work was a real source of redundant cost.

### Hypothesis: Reduce layout similarity overlay overhead

- Change:
  - Replace repeated similarity-representative scans and tree-based `set`/`map` usage with lighter collection and grouping.
- Result:
  - Small help or neutral overall, but safe to keep.
- Observed effect:
  - Did not materially move the full benchmark, but removed obvious repeated widget scanning and reduced overlay bookkeeping.
- Conclusion:
  - The similarity overlay is not the dominant limiter, but the refactor is still cleaner and cheaper.

### Hypothesis: Stop prebuilding every cumulative gauge usage path during relayout

- Change:
  - Keep the per-segment gauge paths and shared track path, but lazily build just the currently needed cumulative usage path on demand during draw instead of materializing all cumulative fill combinations during every `ResolveLayoutState`.
- Result:
  - Helped materially.
- Observed effect:
  - `drag_loop per_iter_ms` improved from about `6.08` to about `4.80`.
  - `snap avg_ms` improved from about `1.42` to about `0.47`.
  - `apply avg_ms` improved from about `0.63` to about `0.37`.
  - `paint_draw avg_ms` stayed about flat at `3.83` to `3.89`.
- Conclusion:
  - Prebuilding unused cumulative gauge fill paths was the largest remaining source of avoidable relayout churn in the drag path, and deferring that work until the one live fill count is known is worth keeping.

### Hypothesis: Use an extent-only layout resolve for snap-preview probes

- Change:
  - Keep renderer-side preview weight application, but resolve only the card and widget rectangles needed for extent comparison during snap probing instead of instantiating widgets, finalizing class groups, rebuilding widget layout state, and rebuilding edit artifacts on every tentative guide weight.
- Result:
  - Helped materially.
- Observed effect:
  - `drag_loop per_iter_ms` improved from about `4.80` to about `4.50`.
  - `snap avg_ms` improved from about `0.47` to about `0.19` to `0.20`.
  - `apply avg_ms` stayed about flat at `0.37` to `0.38`.
  - `paint_draw avg_ms` stayed about flat or slightly better at `3.78` to `3.87`.
- Conclusion:
  - Snap probing only needs resolved extents, so reusing the full widget-state resolve path for those tentative weights was avoidable overhead and is worth skipping.

### Hypothesis: Reuse draw-time text layout and batch static text-anchor measurement

- Change:
  - Let anchor-bearing text draws reuse the text rectangle already computed during `DrawTextBlock` instead of measuring the same text again for dynamic anchor registration, and measure static text anchors through one shared HDC during static-anchor rebuilds instead of calling `GetDC` and `ReleaseDC` per anchor.
- Result:
  - Helped materially.
- Observed effect:
  - `drag_loop per_iter_ms` improved from about `5.05-5.55` down to about `4.54-4.81`.
  - `snap avg_ms` tightened from about `0.21-0.22` down to about `0.20`.
  - `apply avg_ms` improved from about `0.42-0.47` down to about `0.38-0.40`.
  - `paint_draw avg_ms` improved from about `4.17-4.57` down to about `3.82-4.05`.
- Conclusion:
  - Text-anchor bookkeeping still contained measurable repeated text-layout and DC-management overhead, and removing that duplicate work is worth keeping.

### Hypothesis: Reuse resolved widget metrics across unchanged snapshots

- Change:
  - Keep one renderer-owned `DashboardMetricSource` alive across draw calls and invalidate it only when the incoming `SystemSnapshot` revision changes, instead of rebuilding throughput smoothing and per-widget metric caches on every drag repaint.
- Result:
  - Helped modestly.
- Observed effect:
  - `drag_loop per_iter_ms` improved from about `4.54-4.56` down to about `4.44-4.46` on `480`-iteration reruns.
  - `paint_draw avg_ms` improved from about `3.82-3.84` down to about `3.73-3.76`.
- Conclusion:
  - Drag repaints often reuse the same telemetry snapshot, so keeping the resolved metric cache alive until telemetry publishes a newer snapshot revision removes real paint-path churn and is worth keeping.

### Hypothesis: Remove temporary text-cache key construction in renderer text lookups

- Change:
  - Use heterogenous cache lookup keys for cached wide-text, text-width, and text-layout queries so draw-time cache hits avoid constructing owned temporary string keys first.
- Result:
  - Neutral or too small to credit separately, but safe to keep.
- Observed effect:
  - Current reruns with this cleanup in place stay inside about `drag_loop per_iter_ms=4.45-4.59` and `paint_draw avg_ms=3.75-3.88`, which does not move clearly beyond the renderer-metric-cache win on its own.
- Conclusion:
  - The text-cache key churn is real overhead, but this specific cleanup does not move the full drag benchmark enough to treat it as an independent win.

### Hypothesis: Reuse cached overlay pens for layout-edit guide and highlight draws

- Change:
  - Route hovered-widget outlines, layout-edit guides, widget-edit guides, and similarity-indicator strokes through the renderer's existing solid-pen cache instead of creating and deleting short-lived GDI pens every frame.
- Result:
  - Neutral or too small to credit separately, but safe to keep.
- Observed effect:
  - Post-change reruns stayed in the same `drag_loop per_iter_ms=4.45-4.59` and `paint_draw avg_ms=3.75-3.88` band.
- Conclusion:
  - Short-lived overlay pen churn is not a dominant limiter after the larger draw-path fixes, so this cleanup is acceptable to keep but is not a major benchmark lever by itself.

### Hypothesis: Pre-resolve throughput and gauge display payloads inside `DashboardMetricSource`

- Change:
  - Preformat throughput and gauge value strings, pre-normalize throughput history, and precompute throughput guide ratios and marker offsets inside the cached metric source so draw no longer performs that preparation work per frame.
- Result:
  - Regressed and was reverted.
- Observed effect:
  - `drag_loop per_iter_ms` rose to about `5.13` on `480`-iteration reruns and about `5.83` on `240`-iteration reruns.
  - `paint_draw avg_ms` rose to about `4.31` to `4.91`.
- Why it failed:
  - The larger cached payloads and extra per-metric allocations outweighed the saved per-frame formatting and normalization work.
- Conclusion:
  - Keep the cached metric payloads compact; not every per-snapshot prebake helps the hot draw path.

### Hypothesis: Replace throughput `Polyline` with incremental `MoveToEx` and `LineTo`

- Change:
  - Draw the throughput line graph segment-by-segment instead of using `Polyline`.
- Result:
  - Regressed badly and was reverted.
- Observed effect:
  - `paint_draw` jumped to about `7.79 ms`.
- Conclusion:
  - `Polyline` is substantially faster than issuing many individual line commands for this graph.

### Hypothesis: Cache throughput graph chrome in an offscreen bitmap

- Change:
  - Cache the graph background, grid lines, axes, and max label separately from the live data line.
- Result:
  - Regressed and was reverted.
- Observed effect:
  - `paint_draw` rose to about `3.98 ms`.
- Why it failed:
  - The cache-management and copy cost outweighed the saved primitive drawing under the benchmark workload.
- Conclusion:
  - Size and content change often enough during drag that this cache path does not pay for itself here.

### Hypothesis: Reuse measured text rects for dynamic anchor registration

- Change:
  - Reuse text-layout results from draw calls instead of measuring text again for dynamic text anchors.
- Result:
  - Regressed and was reverted.
- Observed effect:
  - `paint_draw` rose to about `4.12 ms`.
- Why it failed:
  - `DrawTextBlock` adds layout work to the fast single-line text path, so avoiding one later measurement cost more than it saved.
- Conclusion:
  - The current fast single-line draw path is more valuable than consolidating measurement into the draw call.

### Hypothesis: Replace text-anchor `DT_CALCRECT` measurement with cached single-line extents

- Change:
  - Add a fast single-line text-layout path inside `MeasureTextBlock` so text-anchor registration can avoid `DrawTextW(...DT_CALCRECT)` for simple one-line labels.
- Result:
  - Neutral to slightly worse and was reverted.
- Observed effect:
  - Post-rebuild reruns stayed around `drag_loop per_iter_ms=5.34` to `5.47`, `snap avg_ms=0.22` to `0.23`, `apply avg_ms=0.45`, and `paint_draw avg_ms=4.38` to `4.52`.
- Why it failed:
  - The anchor-registration measurement cost is not isolated enough for this micro-optimization to move the real drag benchmark, and the extra branching did not produce a clear win.
- Conclusion:
  - Text-anchor work still matters, but this specific single-line measurement shortcut is not worth keeping as-is.

### Hypothesis: Reuse a throughput plot-point scratch buffer

- Change:
  - Reuse a vector for plot points instead of allocating a fresh vector each frame.
- Result:
  - Regressed and was reverted.
- Observed effect:
  - `paint_draw` rose to about `4.01 ms`.
- Why it failed:
  - Allocation overhead was not the dominant cost; the extra mutable state and reuse pattern did not improve the hot path.
- Conclusion:
  - Throughput graph cost is in drawing and point generation itself, not in vector allocation alone.

### Hypothesis: Precompute throughput header label layout during relayout

- Change:
  - Move the static throughput header-label measurement out of `ThroughputWidget::Draw` and into `ResolveLayoutState`, then draw from the premeasured rect during paint.
- Result:
  - Regressed badly and was reverted.
- Observed effect:
  - `drag_loop per_iter_ms` rose to about `11.95`.
  - `snap avg_ms` rose to about `5.19`.
  - `apply avg_ms` rose to about `1.65`.
- Why it failed:
  - The drag path resolves layout every frame, so moving text measurement from draw into relayout multiplied the cost in the snap and apply phases instead of removing meaningful repeated paint work.
- Conclusion:
  - Throughput header-label measurement is cheap enough in the current draw path that relocating it into per-frame relayout is the wrong trade.

### Hypothesis: Fill gauge segments individually instead of filling combined paths

- Change:
  - Replace combined-path gauge fills with repeated per-segment `Gdiplus::Graphics::FillPath` calls.
- Result:
  - Regressed catastrophically and was reverted.
- Observed effect:
  - `paint_draw` rose to about `7.42 ms`.
- Why it failed:
  - Repeated GDI+ fill calls are much more expensive than filling the already-combined path once.
- Conclusion:
  - The combined-path gauge rendering is better than per-segment filling and should remain the baseline.

### Hypothesis: Replace gauge GDI+ path fills with software rasterization

- Change:
  - Rasterize the segmented gauge into a temporary supersampled ARGB image in software and blit that image into the frame instead of using the existing GDI+ path fills.
- Result:
  - Regressed catastrophically and was reverted.
- Observed effect:
  - `drag_loop per_iter_ms` rose to about `8.31`.
  - `snap avg_ms` stayed about `0.22`.
  - `apply avg_ms` stayed about `0.43`.
  - `paint_draw avg_ms` rose to about `7.38`.
- Why it failed:
  - The per-pixel rasterization and extra image-blit path cost much more than the saved GDI+ fill calls.
- Conclusion:
  - A software gauge path is not promising unless it can draw directly into the renderer-owned frame buffer without the extra per-widget raster-and-blit cost.

### Hypothesis: Replace gauge combined-path fills with GDI+ stroked arc segments

- Change:
  - Draw the gauge track, usage, and peak ghost with thick `Gdiplus::Graphics::DrawArc` strokes that use flat caps instead of filling combined annular segment paths.
- Result:
  - Regressed catastrophically and was reverted.
- Observed effect:
  - `drag_loop per_iter_ms` rose to about `11.62` on `240` iterations and about `9.92` on `480` iterations.
  - `paint_draw avg_ms` rose to about `10.73` on `240` iterations and about `9.13` on `480` iterations.
- Why it failed:
  - GDI+ stroked arcs are much slower than the existing combined fill-path gauge draw in this workload.
- Conclusion:
  - Do not retry stroked-arc gauge segments as a substitute for the current combined fill-path gauge rendering.

## Practical Guidance For Future Experiments

- Do not retry per-segment gauge fills unless the gauge is redesigned to avoid repeated GDI+ path fills entirely.
- Do not retry GDI+ stroked-arc gauge segments in place of the current combined fill paths.
- Do not retry throughput `MoveToEx` and `LineTo` in place of `Polyline` for the current graph shape.
- Be skeptical of caches tied to graph or gauge size during drag; size changes every frame and cache maintenance can outweigh the saved draw work.
- Prioritize experiments that reduce primitive count or switch to a cheaper primitive family while preserving the same pixels.
- The most promising remaining directions are:
  - reducing throughput graph draw cost without replacing `Polyline` with per-segment line commands
  - reducing the amount of GDI+ work inside `GaugeWidget::Draw`
  - exploring a renderer-owned direct-to-backbuffer raster path if a software widget experiment can avoid the extra bitmap copy that made the standalone gauge rasterizer fail
  - favoring draw-path work over additional snap-path work unless a new experiment also moves `apply`

## Validation Notes

- Keep the benchmark comparison on the same command line: `build\SystemTelemetryBenchmarks.exe 240 2`.
- Use `profile_benchmark.cmd 240 2` directly for profiling validation; it rebuilds automatically through the daemon workflow.
- If an experiment regresses, revert it and record the result here before finishing.
