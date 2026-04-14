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
  - `drag_loop per_iter_ms=4.80`
  - `snap avg_ms=0.47`
  - `paint_draw avg_ms=3.83`
- Current repeatable result on the optimized tree:
  - `drag_loop per_iter_ms=4.80` to `4.86`
  - `snap avg_ms=0.47`
  - `apply avg_ms=0.37`
  - `paint_draw avg_ms=3.83` to `3.88`

## Current Confirmed Hotspots

Confirmed inclusive hotspots from the latest useful WPR capture in this workstream:

- `DashboardRenderer::Draw` about `44%`
- `DashboardLayoutResolver::ResolveLayout` about `19%`
- `ThroughputWidget::Draw` about `12%`
- ``anonymous namespace'::DrawGraph`` about `12%`
- `GaugeWidget::Draw` about `11%`
- `DashboardRenderer::RegisterTextAnchor` and `RegisterStaticTextAnchor` both around `10%`

Interpretation:

- Snap-path work is no longer the main limiter after the latest gauge-layout optimization.
- The remaining wall time is now dominated by real paint work, especially throughput graph drawing and gauge fills.
- Layout resolution still matters, but its cost dropped materially once gauge relayout stopped prebuilding unused cumulative fill paths.

## Kept Optimizations

These changes produced real wins and remain in the codebase:

- Avoid full config copies during snap evaluation by applying preview weights directly in the renderer and resolving layout from there.
- Group snap candidates by widget so one snap search can serve multiple target extents with shared extent evaluation.
- Refactor layout similarity indicator collection to avoid repeated representative scans and reduce per-frame container churn.
- Build only the one live gauge usage-fill path that the current metric needs instead of prebuilding every cumulative gauge fill path during each relayout.
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

## Practical Guidance For Future Experiments

- Do not retry per-segment gauge fills unless the gauge is redesigned to avoid repeated GDI+ path fills entirely.
- Do not retry throughput `MoveToEx` and `LineTo` in place of `Polyline` for the current graph shape.
- Be skeptical of caches tied to graph or gauge size during drag; size changes every frame and cache maintenance can outweigh the saved draw work.
- Prioritize experiments that reduce primitive count or switch to a cheaper primitive family while preserving the same pixels.
- The most promising remaining directions are:
  - replacing expensive gauge fill operations with a visually equivalent but cheaper drawing method
  - reducing throughput graph draw cost without replacing `Polyline` with per-segment line commands
  - reducing the amount of GDI+ work inside `GaugeWidget::Draw`
  - trimming text-anchor registration cost when layout-edit behavior can stay identical

## Validation Notes

- Keep the benchmark comparison on the same command line: `build\SystemTelemetryBenchmarks.exe 240 2`.
- Use `profile_benchmark.cmd 240 2` directly for profiling validation; it rebuilds automatically through the daemon workflow.
- If an experiment regresses, revert it and record the result here before finishing.
