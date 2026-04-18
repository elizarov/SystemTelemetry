# Profile Benchmark Log

## Purpose

This file records the current benchmark baselines, the latest confirmed hotspots, and the optimization hypotheses that have already been tested. Keep it current after benchmark or profiling work so future experiments can build on prior results instead of repeating failed ideas.

## Benchmark Workflow

- Start the elevated daemon once with `profile_benchmark.cmd /daemon-start` when repeated unattended profiling runs are needed.
- Measure the repeatable layout-edit benchmark with `build\SystemTelemetryBenchmarks.exe edit-layout 240 2`.
- Measure the repeatable telemetry-refresh benchmark with `build\SystemTelemetryBenchmarks.exe update-telemetry 240 2`.
- Capture a full profile with `profile_benchmark.cmd edit-layout 240 2` or `profile_benchmark.cmd update-telemetry 240 2` when a change materially moves that benchmark or when hotspot confirmation is needed.
- The benchmark host forces Direct2D immediate-present mode so direct benchmark runs measure renderer work instead of blocking on desktop-compositor refresh pacing.
- Treat the timing lines printed in the elevated daemon console during `profile_benchmark.cmd` as profiler-instrumented wall-clock numbers, not as the repeatable baseline; compare regressions against the direct `build\SystemTelemetryBenchmarks.exe` runs instead.
- Daemon-backed and one-shot elevated runs persist the benchmark stdout in the request directory and replay it in the caller window after the request finishes, so the requesting shell sees the same timing lines that the elevated process produced.
- `profile_benchmark.cmd` rebuilds automatically through the daemon path, so profiling runs do not need a separate preceding `build.cmd` step.
- Treat direct `build\SystemTelemetryBenchmarks.exe <benchmark> 240 2` runs as the fast comparison loop and the WPR profile as hotspot validation.

## Current Known Baseline

- Original `edit-layout` baseline before the drag-path optimizations in this workstream:
  - `drag_loop per_iter_ms=7.24`
  - `snap avg_ms=2.34`
  - `paint_draw avg_ms=3.96`
- Best measured `edit-layout` result reached during this workstream:
  - `drag_loop per_iter_ms=2.45`
  - `snap avg_ms=0.20`
  - `apply avg_ms=0.22`
  - `paint_draw avg_ms=1.98`
- Current repeatable `edit-layout` result on the current tree:
  - `drag_loop per_iter_ms=2.48` to `2.68`
  - `snap avg_ms=0.19` to `0.20`
  - `apply avg_ms=0.12` to `0.13`
  - `paint_draw avg_ms=2.17` to `2.35`
- Current repeatable `update-telemetry` result on the current tree:
  - `update_loop per_iter_ms=6.15` to `6.23`
  - `telemetry_update avg_ms=4.22` to `4.34`
  - `paint_total avg_ms=1.88` to `1.94`
  - `paint_draw avg_ms=1.88` to `1.94`

## Current Confirmed Hotspots

Current useful hotspot signals from the latest daemon-backed WPR capture on the full-D2D tree:

- The latest daemon-backed `update-telemetry` capture under `build\profile_benchmark_daemon\requests\27365_8562_18495\` keeps the benchmark-process inclusive module weight centered on `PDH.DLL`, `clr.dll`, `amdxx64.dll`, `d2d1.dll`, `DWrite.dll`, `TextShaping.dll`, and smaller remaining `iphlpapi.dll` work.
- The current uncached capture stays clearly collector-bound: `TelemetryCollector::UpdateSnapshot()` is still a bit more than twice the repaint cost on this machine.
- `PDH.DLL` remains the clearest steady-state collector hotspot, with the Gigabyte CLR-backed board provider and the AMD vendor GPU provider still immediately behind it in the current no-cache benchmark shape.
- The Direct2D and DirectWrite frame is still the dominant repaint cost, but it is no longer the main limiter for `update-telemetry`.
- The fast direct reruns and the daemon-backed capture agree closely enough that the reduced uncached collector cost is real even though the exported text view still does not fully resolve every app-owned leaf by symbol.

Interpretation:

- Snap-path work is no longer the main limiter after the latest preview-resolve optimization.
- The remaining cost in the benchmarked live window path is now mostly in the Direct2D, DirectWrite, text-shaping, and driver stack rather than in any remaining app-side GDI or GDI+ icon work.
- Snap and apply work are no longer the main limiter on this tree; the benchmark is primarily measuring the HWND-backed Direct2D/DirectWrite frame.
- The direct `update-telemetry` benchmark now measures the real collector path instead of a synthetic snapshot-mutation loop, and the current no-cache split lands at roughly `4.22` to `4.34 ms` in `TelemetryCollector::UpdateSnapshot()` versus `1.88` to `1.94 ms` in repaint on this machine.
- Future hotspot confirmation for this tree should prefer the call-tree HTML or a richer symbolized WPA view instead of the flat text export, because the flat export is now too coarse to attribute the remaining app-side draw cost precisely inside `PDH.DLL`, the board CLR path, the AMD vendor-provider path, and the Direct2D plus DirectWrite stack.

## Kept Optimizations

These changes produced real wins and remain in the codebase:

- Avoid full config copies during snap evaluation by applying preview weights directly in the renderer and resolving layout from there.
- Group snap candidates by widget so one snap search can serve multiple target extents with shared extent evaluation.
- Refactor layout similarity indicator collection to avoid repeated representative scans and reduce per-frame container churn.
- Build only the one live gauge usage-fill path that the current metric needs instead of prebuilding every cumulative gauge fill path during each relayout.
- Resolve snap-preview guide probes through an extent-only layout pass that skips widget instantiation, widget layout-state caching, and edit-artifact rebuilds.
- Reuse draw-time text layout results for dynamic text anchors and keep all text-anchor measurement on the renderer's shared DirectWrite layout path.
- Reuse one cached `DashboardMetricSource` across successive paints while the resolved `SystemSnapshot` revision stays unchanged, so drag frames reuse smoothed throughput history and formatted metric payloads until telemetry publishes a newer snapshot.
- Fix the title-hover regression introduced during optimization work so card title text highlights correctly again.
- Remove the legacy renderer GDI fallback path and keep both live repaint and screenshot export on the same Direct2D and DirectWrite scene.
- Decode embedded panel icons through WIC and scale them with `IWICBitmapScaler` before upload into render-target-local Direct2D bitmaps, so the renderer no longer depends on GDI+ for icon resources.
- Keep project-owned render-space geometry, color, stroke, and text-style types across the renderer and widget pipeline instead of passing Win32 `RECT`, `POINT`, `HFONT`, `COLORREF`, or `DT_*` contracts through the hot path.

## Tested Hypotheses

### Hypothesis: Steady-state telemetry refresh cost is mostly repaint, not snapshot mutation

- Change:
  - Add a second `update-telemetry` benchmark mode to `SystemTelemetryBenchmarks`, thread that selector through `profile_benchmark.cmd`, and measure a loop that constructs the real `TelemetryCollector`, uses the collector-resolved runtime config, calls `TelemetryCollector::UpdateSnapshot()` each iteration, and repaints the collector-owned snapshot without inserting any timer wait.
- Result:
  - Rejected.
- Observed effect:
  - `build\SystemTelemetryBenchmarks.exe update-telemetry 60 2` ran at `update_loop per_iter_ms=12.01`, `telemetry_update avg_ms=9.97`, and `paint_draw avg_ms=2.04`.
  - `build\SystemTelemetryBenchmarks.exe update-telemetry 120 2` ran at `update_loop per_iter_ms=10.90`, `telemetry_update avg_ms=8.90`, and `paint_draw avg_ms=2.00`.
- Conclusion:
  - The earlier repaint-bound result came from a synthetic snapshot-mutation loop and does not describe the real app path. With the real collector in place, steady-state telemetry refresh is collector-bound on this tree, so future `update-telemetry` wins should focus on `TelemetryCollector::UpdateSnapshot()` and its provider work before chasing another millisecond out of repaint.

### Hypothesis: Cache slow-changing collector inputs and narrow the per-refresh network query path

- Change:
  - Reuse board-vendor and GPU-vendor samples for one second, reuse per-drive capacity and free-space metadata for five seconds while still refreshing storage throughput each tick, and replace the per-refresh `GetIfTable2` walk in steady-state network updates with `GetIfEntry2` against the already selected adapter.
- Result:
  - Helped materially, but was backed out.
- Observed effect:
  - Before the change, `build\SystemTelemetryBenchmarks.exe update-telemetry 120 2` ran at `update_loop per_iter_ms=10.08`, `telemetry_update avg_ms=8.06`, and `paint_draw avg_ms=2.02`.
  - After the change, `build\SystemTelemetryBenchmarks.exe update-telemetry 120 2` ran at `update_loop per_iter_ms=4.17`, `telemetry_update avg_ms=2.34`, and `paint_draw avg_ms=1.83`.
  - The follow-up daemon-backed run `profile_benchmark.cmd update-telemetry 240 2` landed at `update_loop per_iter_ms=4.41`, `telemetry_update avg_ms=2.42`, and `paint_draw avg_ms=2.00`.
- Conclusion:
  - The cache-backed version proves those sources are expensive, but the current benchmark intentionally leaves board, GPU-vendor, and drive metadata uncached so steady-state profiles keep stressing the full telemetry path and expose which source is intrinsically slow.

### Hypothesis: Trim telemetry bookkeeping around live API calls without reusing sampled values

- Change:
  - Keep every telemetry source live on every update, but stop building dynamic trace strings when no trace stream is attached, reuse the GPU PDH array scratch buffer, reuse per-drive root-path state instead of rebuilding it per refresh, cache ADLX support flags discovered at initialization, and stop rebuilding verbose provider diagnostics payloads on each successful vendor sample.
- Result:
  - Helped materially.
- Observed effect:
  - Before the change, `build\SystemTelemetryBenchmarks.exe update-telemetry 240 2` ran at `update_loop per_iter_ms=7.75`, `telemetry_update avg_ms=5.58`, and `paint_draw avg_ms=2.18`.
  - After the change, `build\SystemTelemetryBenchmarks.exe update-telemetry 240 2` ran at `update_loop per_iter_ms=6.19`, `telemetry_update avg_ms=4.28`, and `paint_draw avg_ms=1.91`.
  - The follow-up daemon-backed run `profile_benchmark.cmd update-telemetry 240 2` landed at `update_loop per_iter_ms=6.16`, `telemetry_update avg_ms=4.22`, and `paint_draw avg_ms=1.94`.
- Conclusion:
  - The no-cache benchmark still spends most of its time in the real telemetry APIs, but a meaningful slice of the old cost was collector-side scaffolding around those calls. Lazy trace formatting and reusable per-provider scratch state are worth keeping because they reduce benchmark noise without hiding the real source-update cost.

### Hypothesis: Collapse the GPU PDH path into one query collect and one array scan

- Change:
  - Put the GPU dedicated-memory counter onto the same PDH query as the GPU engine-utilization counter and replace the two `PdhGetFormattedCounterArrayW` scans for GPU load with one combined array walk that computes both the 3D-only total and the all-engine total.
- Result:
  - Regressed and was reverted.
- Observed effect:
  - Before the change, `build\SystemTelemetryBenchmarks.exe update-telemetry 240 2` ran at about `update_loop per_iter_ms=6.19`, `telemetry_update avg_ms=4.28`, and `paint_draw avg_ms=1.91`.
  - With the collapsed GPU PDH path in place, the same benchmark regressed to about `update_loop per_iter_ms=7.07`, `telemetry_update avg_ms=5.06`, and `paint_draw avg_ms=2.01`.
- Why it failed:
  - Combining the GPU memory and engine counters onto one PDH query and scanning the shared wildcard arrays together increased the measured hot-path cost instead of reducing it, likely because the merged query shape or the broader array fetch paid more than the saved call count.
- Conclusion:
  - Do not assume fewer PDH calls automatically help. On this tree, the split GPU query shape is faster than the collapsed variant and remains the better live-update baseline.

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

### Hypothesis: Replace Win32 renderer-contract types with renderer-owned D2D-native contract types

- Change:
  - Introduce shared render-space `RenderPoint`, `RenderRect`, `RenderColor`, `RenderStroke`, `TextStyleId`, and `TextLayoutOptions` types, migrate renderer and widget contracts onto those types, move renderer text style lookup to DirectWrite text-format ids, and keep raw Win32 input and GDI overlay types only at the `DashboardApp` shell boundary.
- Result:
  - Neutral to slightly helpful, and kept.
- Observed effect:
  - `build\SystemTelemetryBenchmarks.exe 240 2` ran at `drag_loop per_iter_ms=2.48`, `snap avg_ms=0.20`, `apply avg_ms=0.27`, and `paint_draw avg_ms=2.00`.
  - `build\SystemTelemetryBenchmarks.exe 480 2` ran at `drag_loop per_iter_ms=2.45`, `snap avg_ms=0.20`, `apply avg_ms=0.27`, and `paint_draw avg_ms=1.98`.
  - `profile_benchmark.cmd 240 2` kept the benchmark hotspot shape in `d2d1.dll`, `DWrite.dll`, `TextShaping.dll`, the display driver, and `win32kfull.sys`, with no new GDI text hotspot.
- Conclusion:
  - The type migration is safe to keep. It simplifies the Direct2D renderer boundary without costing measurable draw-path time and keeps raw Win32 types confined to the shell message-handling and placement-tracking edge.

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

### Hypothesis: Keep throughput header labels on the cheap single-line text draw path

- Change:
  - Draw the throughput metric label with `DrawText` plus cached `MeasureTextWidth` instead of `DrawTextBlock`, because that header label only needs a width to place the value text and does not register a text anchor.
- Result:
  - Helped.
- Observed effect:
  - `build\SystemTelemetryBenchmarks.exe 240 2` reruns improved from about `drag_loop per_iter_ms=2.60-2.68`, `snap avg_ms=0.19-0.20`, `apply avg_ms=0.13`, and `paint_draw avg_ms=2.28-2.35` down to about `drag_loop per_iter_ms=2.54-2.57`, `snap avg_ms=0.18-0.19`, `apply avg_ms=0.12`, and `paint_draw avg_ms=2.22-2.27`.
  - `profile_benchmark.cmd 240 2` stayed on the same Direct2D, DirectWrite, text-shaping, and driver-stack hotspot shape, with no new app-owned hotspot overtaking the frame.
- Conclusion:
  - Single-line labels that only need placement width should stay on the cheaper `DrawText` path; using `DrawTextBlock` for those labels adds measurable DirectWrite layout work without paying for any anchor or wrapped-layout benefit.

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

### Hypothesis: Move throughput and gauge drawing onto a Direct2D DC render target

- Change:
  - Add a renderer-owned Direct2D DC render target, switch throughput graph primitives and gauge fills to Direct2D geometry draws, and test both per-widget bind/end draws and one frame-scoped bind with explicit flushes before the remaining GDI text.
- Result:
  - Regressed catastrophically and was reverted.
- Observed effect:
  - Per-widget Direct2D bind/end draws pushed `drag_loop per_iter_ms` to about `15.24` and `paint_draw avg_ms` to about `14.24`.
  - One frame-scoped Direct2D session with explicit flushes still stayed much worse at about `drag_loop per_iter_ms=9.45` and `paint_draw avg_ms=8.49`.
- Why it failed:
  - Binding a Direct2D DC render target onto the current compatible-bitmap backbuffer adds much more interop cost than the saved primitive work on this benchmark path.
- Conclusion:
  - Do not retry Direct2D through `ID2D1DCRenderTarget` on top of the current GDI memory-DC backbuffer. If Direct2D is revisited, it needs a real HWND or DXGI-owned target path rather than this interop layer.

### Hypothesis: Move throughput and gauge drawing onto a real HWND-backed Direct2D target

- Change:
  - Add a renderer-owned `ID2D1HwndRenderTarget` plus GDI interop passes, route throughput graph primitives and gauge fills through that hybrid window-backed Direct2D path, and benchmark it against the same layout-edit drag workload.
- Result:
  - Regressed and was reverted.
- Observed effect:
  - `drag_loop per_iter_ms` rose to about `7.73` to `7.74`.
  - `snap avg_ms` rose to about `0.24`.
  - `apply avg_ms` rose to about `0.55` to `0.62`.
  - `paint_draw avg_ms` rose to about `6.87` to `6.94`.
- Why it failed:
  - The mixed GDI and Direct2D window passes still paid enough interop and extra frame-pass overhead to outweigh the faster primitive drawing.
- Conclusion:
  - A partial real-HWND Direct2D migration is not competitive on this benchmark path. If Direct2D is revisited, it likely needs a more complete renderer rewrite that avoids the mixed GDI and Direct2D pass structure.

### Hypothesis: Rewrite the live window draw path around Direct2D and DirectWrite

- Change:
  - Add a renderer-owned `ID2D1HwndRenderTarget` plus `IDWriteFactory`, route the live window frame through `DashboardRenderer::DrawWindow`, port renderer text and primitive helpers to Direct2D and DirectWrite, move throughput and gauge drawing onto Direct2D geometry draws, export screenshots through a WIC-backed Direct2D render target that reuses the same scene, and refresh the font and layout resources once on the first Direct2D draw so startup and headless exports keep the full text path active.
- Result:
  - Helped materially.
- Observed effect:
  - `240`-iteration reruns land around `drag_loop per_iter_ms=2.59` to `2.61`.
  - `snap avg_ms` stays about `0.20` to `0.21`.
  - `apply avg_ms` lands about `0.23` to `0.24`.
  - `paint_draw avg_ms` lands about `2.15` to `2.17`.
- Why it helped:
  - The benchmarked live repaint no longer pays the compatible-DC backbuffer and mixed GDI/GDI+ pass structure. One real HWND-backed Direct2D and DirectWrite frame with the full text path active is still much cheaper than any of the hybrid interop variants that were tested earlier.
- Conclusion:
  - A full renderer rewrite onto a real window-backed Direct2D target is the first Direct2D path that clearly beats the old renderer on this workload, and future draw-path work should treat this all-D2D live path as the current baseline.

### Hypothesis: Remove the old GDI fallback path after the full Direct2D rewrite

- Change:
  - Delete the renderer's legacy GDI fallback draw path, remove the `HDC`-threaded widget and renderer draw entry points, route the app paint loop and the drag benchmark harness through `DashboardRenderer::DrawWindow` only, and keep screenshots on the same WIC-backed Direct2D scene.
- Result:
  - Neutral for throughput and worth keeping for simplicity.
- Observed effect:
  - `240`-iteration reruns landed at `drag_loop per_iter_ms=2.53`, `snap avg_ms=0.20`, `apply avg_ms=0.22`, and `paint_draw avg_ms=2.11`.
  - `480`-iteration reruns landed at `drag_loop per_iter_ms=2.58`, `snap avg_ms=0.21`, `apply avg_ms=0.23`, and `paint_draw avg_ms=2.13`.
- Why it helped:
  - The cleanup did not materially change the already-fast Direct2D frame cost, but it removed a second renderer path that could drift out of sync with the live scene and hide correctness problems in diagnostics or benchmarks.
- Conclusion:
  - Keep the renderer D2D-only. Future draw-path work should optimize the live Direct2D scene directly instead of preserving a dormant GDI fallback.

### Hypothesis: Remove the renderer-wide UTF-8 to UTF-16 cache from the draw path

- Change:
  - Stop routing `DrawText`, `DrawTextBlock`, `MeasureTextBlock`, and `MeasureTextWidth` through the renderer-wide `GetWideText` cache, and convert draw strings to UTF-16 on demand so changing telemetry values do not accumulate in a long-lived cache.
- Result:
  - Neutral to slightly positive and worth keeping for correctness.
- Observed effect:
  - `240`-iteration reruns landed at `drag_loop per_iter_ms=2.53`, `snap avg_ms=0.20`, `apply avg_ms=0.23`, and `paint_draw avg_ms=2.11`.
  - `480`-iteration reruns landed at `drag_loop per_iter_ms=2.52`, `snap avg_ms=0.21`, `apply avg_ms=0.22`, and `paint_draw avg_ms=2.09`.
- Why it helped:
  - The cache was not buying measurable benchmark throughput, and removing it prevents unbounded retention of ever-changing telemetry strings such as clock and live numeric readouts.
- Conclusion:
  - Keep uncached UTF-16 conversion in the draw path unless a future bounded cache shows a clear benchmark win without reintroducing unbounded growth.

### Hypothesis: Replace the remaining panel-icon GDI+ decode and scale path with WIC

- Change:
  - Decode embedded PNG panel icons through WIC, keep the cached source bitmaps as `IWICBitmapSource`, scale them with `IWICBitmapScaler`, upload them into render-target-local Direct2D bitmaps on demand, and remove the `gdiplus` link dependency from the app and benchmark targets.
- Result:
  - Neutral for throughput and worth keeping for dependency cleanup.
- Observed effect:
  - `240`-iteration reruns landed at `drag_loop per_iter_ms=2.49` to `2.50`, `snap avg_ms=0.20`, `apply avg_ms=0.28`, and `paint_draw avg_ms=2.01` to `2.02`.
- Why it helped:
  - The benchmark keeps the same Direct2D and DirectWrite hotspot shape while removing the last benchmark-process `GdiPlus.dll` dependency and keeping icon decode, scale, screenshot export, and bitmap upload on one WIC plus Direct2D asset path.
- Conclusion:
  - Keep the WIC-based icon path. Future renderer cleanup can assume panel icons, screenshots, and the live frame all stay off GDI+.

### Hypothesis: Reuse one GDI+ `Graphics` object across the whole frame

- Change:
  - Keep one frame-scoped GDI+ `Graphics` instance alive and route gauge and editable-anchor highlight draws through it instead of constructing a fresh `Graphics` wrapper for each draw.
- Result:
  - Regressed and was reverted.
- Observed effect:
  - Reruns landed around `drag_loop per_iter_ms=5.31` to `5.66` and `paint_draw avg_ms=4.41` to `4.66`.
- Why it failed:
  - The shared `Graphics` path did not offset its own state-management overhead on this workload, and the original per-draw setup stays cheaper in practice.
- Conclusion:
  - Keep the existing local `Gdiplus::Graphics` construction in the hot gauge and highlight paths.

### Hypothesis: Keep the container guide outline but replace the dashed-stroke implementation

- Change:
  - Keep the layout-guide container highlight feedback, but draw the dotted outline with a manual filled-dot pattern instead of a Direct2D dashed stroke.
- Result:
  - Helped materially.
- Observed effect:
  - The current tree before this change measured about `drag_loop per_iter_ms=3.95`, `apply avg_ms=0.55`, and `paint_draw avg_ms=3.20`.
  - Removing the container outline entirely moved the benchmark to about `drag_loop per_iter_ms=3.51`, `apply avg_ms=0.58`, and `paint_draw avg_ms=2.73`, confirming the highlight itself was the dominant new paint regression from that feedback feature.
  - Replacing the dashed stroke with the manual filled-dot outline kept the feedback while landing about `drag_loop per_iter_ms=3.54`, `apply avg_ms=0.59`, and `paint_draw avg_ms=2.76`.
- Conclusion:
  - The feedback itself is acceptable to keep, but the dashed Direct2D stroke is too expensive on the live drag path. Prefer the cheaper manual-dot outline for large container highlights.

### Hypothesis: Avoid full renderer reconfiguration when only layout weights change

- Change:
  - Keep `DashboardRenderer::SetConfig` on the drag apply path, but only rebuild palette state, panel-icon assets, DirectWrite text formats, and metric caches when the corresponding config inputs actually change instead of reloading all renderer resources on every layout-weight update.
- Result:
  - Helped materially.
- Observed effect:
  - With the cheaper container outline already in place, reruns before this change landed around `drag_loop per_iter_ms=3.54`, `snap avg_ms=0.20`, `apply avg_ms=0.59`, and `paint_draw avg_ms=2.76`.
  - After narrowing `SetConfig` to layout-relevant work only, repeatable reruns landed at `drag_loop per_iter_ms=2.60` to `2.68`, `snap avg_ms=0.19` to `0.20`, `apply avg_ms=0.13`, and `paint_draw avg_ms=2.28` to `2.35`.
- Conclusion:
  - The regression was not just in draw; the drag path was also paying avoidable per-frame renderer reconfiguration churn. Preserve caches and resource rebuilds across layout-only config updates.

## Practical Guidance For Future Experiments

- Do not retry per-segment gauge fills unless the gauge is redesigned to avoid repeated GDI+ path fills entirely.
- Do not retry GDI+ stroked-arc gauge segments in place of the current combined fill paths.
- Do not retry throughput `MoveToEx` and `LineTo` in place of `Polyline` for the current graph shape.
- Do not retry Direct2D through `ID2D1DCRenderTarget` on the old compatible-bitmap paint buffer; that interop path is much slower than the current full Direct2D renderer.
- Do not retry partial GDI and Direct2D hybrid window passes; the full window-owned Direct2D and DirectWrite path is the only Direct2D experiment in this workstream that materially helps.
- Do not reintroduce a renderer-side GDI fallback path for live repaint, diagnostics export, or benchmarks; it adds maintenance and can mask problems in the real Direct2D scene without improving the current benchmark.
- Be skeptical of caches tied to graph or gauge size during drag; size changes every frame and cache maintenance can outweigh the saved draw work.
- Prioritize experiments that reduce primitive count or switch to a cheaper primitive family while preserving the same pixels.
- The most promising remaining directions are:
  - reducing throughput graph and gauge geometry cost inside the new Direct2D live path without falling back to hybrid interop
  - using richer WPA views or call trees to isolate the remaining app-side work that the flat text export now hides behind `d2d1.dll`, `DWrite.dll`, and the driver stack
  - reducing DirectWrite and text-shaping cost now that the real text path is active in both live repaint and screenshot export
  - favoring draw-path work over additional snap-path work unless a new experiment also moves `apply`

## Validation Notes

- Keep the benchmark comparison on the same command line shape: `build\SystemTelemetryBenchmarks.exe update-telemetry 240 2` or `build\SystemTelemetryBenchmarks.exe edit-layout 240 2`.
- Use `profile_benchmark.cmd update-telemetry 240 2` or `profile_benchmark.cmd edit-layout 240 2` directly for profiling validation; it rebuilds automatically through the daemon workflow.
- If an experiment regresses, revert it and record the result here before finishing.
