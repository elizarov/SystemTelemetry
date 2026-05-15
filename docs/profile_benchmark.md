# Profile Benchmark Log

This document owns benchmark workflow, current baselines, confirmed hotspots, and performance experiment history.
See also: [docs/build.md](build.md) for build and tooling entrypoints, [docs/optimize_size.md](optimize_size.md) for executable-size research, and [docs/architecture.md](architecture.md) for subsystem structure.

## Purpose

This file records the current benchmark baselines, latest confirmed hotspots, and tested performance hypotheses so future profiling work can build on prior results instead of repeating failed ideas.

## Benchmark Workflow

- Start the elevated daemon once with `profile_benchmark.cmd /daemon-start` when repeated unattended profiling runs are needed.
- Build the benchmark executable with `build.cmd /benchmarks` for direct benchmark runs; normal `build.cmd` and release or validation builds do not build `CaseDashBenchmarks.exe`.
- Measure the animation presenter benchmark with `build\CaseDashBenchmarks.exe animation 240 2`; this benchmark uses fake metrics, builds one no-overlay snapshot layer plus animation list, and then repeatedly presents the stored frame through the same bitmap composition and animation timeline path used by the render thread. It measures short active-transition chunks so every reported frame includes `WidgetAnimationTransition::Sample()` work.
- Measure the repeatable layout-edit benchmark with `build\CaseDashBenchmarks.exe edit-layout 240 2`.
- Measure the in-memory layout-guide-sheet generation benchmark with `build\CaseDashBenchmarks.exe layout-guide-sheet 20 2`.
- Measure the repeatable layout-switch benchmark with `build\CaseDashBenchmarks.exe layout-switch 240 2`.
- Measure the repeatable mouse-hover benchmark with `build\CaseDashBenchmarks.exe mouse-hover 240 2`.
- Measure the repeatable theme-change benchmark with `build\CaseDashBenchmarks.exe theme-change 240 2`.
- Measure the repeatable telemetry-refresh benchmark with `build\CaseDashBenchmarks.exe update-telemetry 240 2`; this benchmark deliberately uses the package-private synchronous collector to measure provider collection CPU instead of the production telemetry runtime thread scheduler.
- `CaseDashBenchmarks` accepts the supported benchmark names `animation`, `edit-layout`, `layout-guide-sheet`, `layout-switch`, `mouse-hover`, `theme-change`, and `update-telemetry` as the first argument; starting it without arguments prints that list and exits without running a benchmark. `profile_benchmark.cmd` uses the same required benchmark-name argument for profiling runs.
- Direct benchmark runs create a disabled trace object without an output stream, so trace formatting and writes do not affect benchmark timing.
- Real `/trace-prefixes:profile` runs emit only `profile:timing` summaries every 10 seconds for comparable runtime operation names such as `telemetry_update`, `paint_draw`, and `animation_frame`; use those summaries to check whether direct benchmark timings match the interactive app on the same machine without paying verbose provider-trace overhead.
- Speed optimizations that replace `std::unordered_map` with a custom hash-based table keep the hash table in a separate owning `.h`/`.cpp` module and record the benchmark result here; large provider, renderer, or controller files do not carry local hashing and probing implementations.
- The mouse-hover benchmark moves the layout-edit cursor path from the dashboard's top-left corner to bottom-right corner, resolving hover hits and drawing the resulting overlay state on every step.
- The theme-change benchmark rotates through all configured themes and measures config copy, color resolution, dashboard reconfiguration, edit-tree rebuild, theme-preview drawing, and dashboard repaint.
- The edit-layout drag benchmark mirrors the app's drag path by applying layout mutations through the app-style config tail, refreshing layout dirty tracking, and forcing one redraw for each synthetic pointer move.
- The animation benchmark keeps layout, telemetry, and layer painting out of the timed loop. It keeps the shipped target state fixed between synthetic telemetry updates and times the render-thread side of each animation frame: retained dirty-window bitmap-region composition, active `WidgetAnimationTransition::Sample()` work between old and target state, timeline state lookup, and widget animation drawing.
- Capture a benchmark-focused CPU profile with `profile_benchmark.cmd animation 2400 2`, `profile_benchmark.cmd edit-layout 240 2`, `profile_benchmark.cmd layout-switch 240 2`, `profile_benchmark.cmd mouse-hover 240 2`, `profile_benchmark.cmd theme-change 240 2`, or `profile_benchmark.cmd update-telemetry 240 2` when a change materially moves that benchmark or when hotspot confirmation is needed. The longer animation run keeps benchmark setup noise below the per-frame presenter work in the profiler call tree.
- Capture a layout-guide-sheet CPU profile with `profile_benchmark.cmd layout-guide-sheet 20 2`; the benchmark renders the sheet to an in-memory offscreen surface and deliberately excludes PNG encoding and file I/O.
- The layout, hover, theme, telemetry, and guide-sheet benchmark hosts force the dashboard renderer's immediate-present path. Direct benchmark runs stay single-threaded, but paint timings include snapshot and overlay bitmap construction plus the final bitmap-and-animation composition step that the live render thread performs in production. The `animation` benchmark instead builds one threaded-style layer frame, then directly drives the single-thread presenter loop around the stored frame.
- Treat the timing lines printed in the elevated daemon console during `profile_benchmark.cmd` as profiler-instrumented wall-clock numbers, not as the repeatable baseline; compare regressions against the direct `build\CaseDashBenchmarks.exe` runs instead.
- Daemon-backed and one-shot elevated runs persist the benchmark stdout and hotspot summary in the request directory and replay both in the caller window after the request finishes, so the requesting shell sees the same timing lines and top hotspots that the elevated process produced.
- Profile captures use a minimal xperf CPU sample trace with process and image-load metadata, profile stack walking, process-filtered stack export for `CaseDashBenchmarks.exe`, a `256 MB` circular ETL cap by default, and a compact hotspot summary generated from the filtered call tree.
- Override the circular ETL cap with `PROFILE_BENCHMARK_MAX_FILE_MB=<size>` and the call-tree cutoff with `PROFILE_BENCHMARK_STACK_MIN_HITS=<hits>` when a specific investigation needs a different tradeoff.
- Treat direct `build\CaseDashBenchmarks.exe <benchmark> 240 2` runs as the fast comparison loop and the xperf profile as hotspot validation.

## Current Known Baseline

- Original `edit-layout` baseline before the drag-path optimizations in this workstream:
  - `drag_loop per_iter_ms=7.24`
  - `snap avg_ms=2.34`
  - `paint_draw avg_ms=3.96`
- Best measured `edit-layout` result reached during this workstream:
  - `drag_loop per_iter_ms=2.11`
  - `snap avg_ms=0.06`
  - `apply avg_ms=0.05`
  - `paint_draw avg_ms=1.99`
- Current repeatable `edit-layout` result on the current tree:
  - `drag_loop per_iter_ms=2.34`
  - `snap avg_ms=0.08`
  - `apply avg_ms=0.06`
  - `paint_draw avg_ms=2.18`
- Current repeatable `animation` result on the current tree:
  - `animation_loop per_iter_ms=0.41`
  - `animation_frame avg_ms=0.41`
  - `snapshot_animations=28`
  - `overlay_animations=0`
  - `active_chunk_frames=120`
- Current repeatable `update-telemetry` result on the current tree:
  - `update_loop per_iter_ms=4.70`
  - `telemetry_update avg_ms=2.58`
  - `paint_total avg_ms=2.12`
  - `paint_draw avg_ms=2.12`
- Current repeatable `layout-switch` result on the current tree:
  - `switch_loop per_iter_ms=3.76`
  - `switch_apply avg_ms=0.86`
  - `dialog_refresh avg_ms=0.24`
  - `switch_paint avg_ms=2.65`
- Current repeatable `theme-change` result on the current tree:
  - `theme_loop per_iter_ms=3.68`
  - `config_copy avg_ms=0.01`
  - `color_resolve avg_ms=0.05`
  - `dashboard_config avg_ms=0.86`
  - `edit_tree avg_ms=0.24`
  - `theme_preview avg_ms=0.30`
  - `theme_paint avg_ms=2.19`
- Current repeatable `mouse-hover` result on the current tree:
  - `hover_loop per_iter_ms=0.76`
  - `hover_hit_test avg_ms=0.23`
  - `paint_total avg_ms=0.52`
  - `paint_draw avg_ms=0.52`
- Current repeatable `layout-guide-sheet` result on the current tree:
  - `sheet_loop per_iter_ms=115.08`
  - `active_regions avg_ms=4.24`
  - `sheet_plan avg_ms=0.86`
  - `sheet_measure avg_ms=3.70`
  - `sheet_place avg_ms=75.61`
  - `sheet_draw avg_ms=30.55`

## Current Confirmed Hotspots

Current useful benchmark and hotspot signals from the latest direct runs and daemon-backed WPR captures on the full-D2D tree:

- The snapshot/overlay animation pipeline now builds layer bitmaps through a dashboard-renderer pool. The main thread acquires writable snapshot and overlay bitmaps by size, and the render thread returns superseded active or pending frame layers to that pool. `D2DRenderer` uses shared Direct2D device bitmaps for live layer storage, keeps WIC-backed layer bitmaps for deterministic export, caches target-local bitmap brushes for repeated animation-frame composition until the pooled bitmap is redrawn, restores non-coalesced dirty regions inside the retained back buffer, and presents animation-capable full redraws plus dirty animation frames through the same DXGI flip-model swap chain.
- The direct `animation` benchmark builds one fake-metric, no-overlay dashboard frame with `28` snapshot animations and repeatedly presents the stored frame through the render-thread presenter path. The first direct reruns after the DXGI flip-model backend landed at `animation_frame avg_ms=0.64` to `0.67`; removing DXGI dirty-present metadata and using the simple swap-chain `Present()` call brings the current direct rerun to `animation_frame avg_ms=0.41`. The renderer primes every physical flip-chain back buffer with a full composition after retained full redraws so dirty-only animation frames do not alternate between stale buffer histories. The previous retained-HWND-target baseline was `0.36` to `0.37`, and the remaining gap is in Direct2D/DXGI/driver flush and present work rather than widget animation sampling.
- The daemon-backed `animation` capture under `build\profile_benchmark_daemon\requests\23068_11347_26552\` showed the dirty-present regression before the presentation fix: `animation_frame avg_ms=0.70`, `D2DRenderer::DrawWindowDirty` was `72.42%` inclusive, `D2DRenderer::EndWindowDraw` was `66.89%`, `D2DRenderer::PresentDxgiWindow` was `36.87%`, and app-side `DashboardRenderThread::DrawPreparedDirtyAnimations` was only `5.13%`.
- The daemon-backed `animation` capture under `build\profile_benchmark_daemon\requests\23845_13564_18385\` after the presentation fix reports `animation_frame avg_ms=0.43`; `D2DRenderer::PresentDxgiWindow` drops to `13.67%` inclusive while `D2DRenderer::EndDirect2DDraw` is `34.56%`, `DashboardRenderThread::DrawPreparedDirtyAnimations` is `9.37%`, and `ThroughputChartAnimationTransition::Sample()` appears only as a `0.31%` exclusive leaf.
- The daemon-backed `animation` capture under `build\profile_benchmark_daemon\requests\16104_12574_23322\` used the previous retained-HWND-target path for `2400` frames and reported `animation_frame avg_ms=0.36` with `active_chunk_frames=120`. The hotspot shape was almost entirely Direct2D present work: `D2DRenderer::DrawWindowDirty` was `73.78%` inclusive and `D2DRenderer::EndDirect2DDraw` was `63.99%` inclusive. App-side work dropped to `DashboardRenderThread::DrawFrameDirty` at `9.79%`, `DashboardRenderThread::DrawPreparedDirtyAnimations` at `8.30%`, and throughput chart drawing at `5.07%`. Dirty animation presentation keeps non-coalesced dirty regions, batches bitmap-region restores, prepares dirty bounds and sampled states in one pass per frame, and draws each prepared animation once.
- The daemon-backed `theme-change` capture under `build\profile_benchmark_daemon\requests\24489_1080_31451\` exposed the repeated theme-preview regression before caching: `theme_preview avg_ms=1.51`, `DrawThemePreviewTriangle` was `24.70%` inclusive, and `FillThemePreviewPixels` was `19.22%` inclusive. After caching generated preview triangle pixels by size, DPI, system face color, and resolved theme colors, the capture under `build\profile_benchmark_daemon\requests\25220_16659_2561\` reports `theme_preview avg_ms=0.31`, and `FillThemePreviewPixels` falls to a `0.16%` exclusive leaf.
- A pre-pool daemon-backed `edit-layout` capture under `build\profile_benchmark_daemon\requests\21261_462_9464\` reported `drag_loop per_iter_ms=7.51`, `paint_draw avg_ms=7.30`, `D2DRenderer::DrawToBitmap` at `35.25%` inclusive hits, and heavy `D3D10Warp.dll` plus `WindowsCodecs.dll` module weight from CPU/WIC-backed layer bitmap churn.
- The daemon-backed `edit-layout` capture after the layer bitmap pool under `build\profile_benchmark_daemon\requests\25725_13470_17626\` reports `drag_loop per_iter_ms=2.37`, `snap avg_ms=0.08`, `apply avg_ms=0.06`, and `paint_draw avg_ms=2.22`; remaining app-inclusive weight sits in `DashboardRenderer::BuildPresentationFrame`, `D2DRenderer::DrawToBitmap`, `DashboardRenderer::DrawSnapshotLayer`, and Direct2D/DirectWrite text drawing. Layout drags still redraw snapshot and overlay layers by design, so this benchmark carries the intentional layer-build plus final-composition cost.
- The final direct benchmark refresh after the layer bitmap pool lands at `edit-layout drag_loop per_iter_ms=2.36`, `update-telemetry update_loop per_iter_ms=4.89`, `layout-switch switch_loop per_iter_ms=3.52`, `theme-change theme_loop per_iter_ms=4.15`, `mouse-hover hover_loop per_iter_ms=1.32`, and `layout-guide-sheet sheet_loop per_iter_ms=114.00`. The overlay-only hover path is faster than the old direct-paint baseline because the retained snapshot layer is reused and only overlay plus composition work runs per hover step.
- The latest daemon-backed `update-telemetry` captures under `build\profile_benchmark_daemon\requests\18014_1564_22035\` and `build\profile_benchmark_daemon\requests\18154_4993_3762\` report `update_loop per_iter_ms=5.42` to `5.84`, `telemetry_update avg_ms=3.08` to `3.46`, and `paint_draw avg_ms=2.34` to `2.37`; `FindRetainedHistory` appears only as a tiny exclusive leaf in one capture at `0.12%`, while the app-inclusive weight stays in `RealTelemetryCollector::UpdateSnapshot`, `AmdAdlxGpuTelemetryProvider::Sample`, `UpdateGpuMetrics`, and Direct2D/DirectWrite paint.
- The latest daemon-backed `update-telemetry` capture under `build\profile_benchmark_daemon\requests\10827_2817_24593\` reports `update_loop per_iter_ms=4.85`, `telemetry_update avg_ms=2.72`, and `paint_draw avg_ms=2.13`; the app-inclusive call tree keeps `RealTelemetryCollector::UpdateSnapshot`, `AmdAdlxGpuTelemetryProvider::Sample`, and `UpdateGpuMetrics` visible while `PresentedFpsEtwProvider::Sample` is only `0.79%` exclusive hits and the GPU raw-counter hash lookup is `0.40%` exclusive hits. The benchmark-process inclusive module weight remains centered on Direct2D, DirectWrite, PDH, Win32, kernel, and AMD driver work rather than app-side process-cache scans.
- A direct idle-process stress run with `300` hidden `timeout.exe` processes alive reported `process_count=927`, `gpu_engine_counters=632`, and `gpu_engine_pids=28`; `build\CaseDashBenchmarks.exe update-telemetry 240 2` still landed at `update_loop per_iter_ms=4.78`, `telemetry_update avg_ms=2.63`, and `paint_draw avg_ms=2.14`.
- The latest direct `update-telemetry` rerun after extracting `GpuRawCounterMap` into `telemetry/fps/impl/` landed at `update_loop per_iter_ms=4.80`, `telemetry_update avg_ms=2.63`, and `paint_draw avg_ms=2.17`.
- The direct reruns after the telemetry metric row-storage size pass landed at `update_loop per_iter_ms=4.76`, `telemetry_update avg_ms=2.57`, `paint_draw avg_ms=2.18`, and `edit-layout paint_draw avg_ms=2.18`; keep the fixed-slot metric and drive-row caches because a single borrowed row slot without fixed reuse made repeated paint noticeably slower in direct reruns.
- The previous full direct benchmark refresh after the size-optimization work landed at `edit-layout drag_loop per_iter_ms=2.15`, `update-telemetry update_loop per_iter_ms=4.46`, `layout-switch switch_loop per_iter_ms=3.52`, `theme-change theme_loop per_iter_ms=4.02`, `mouse-hover hover_loop per_iter_ms=2.13`, and `layout-guide-sheet sheet_loop per_iter_ms=116.62`; keep those numbers as the pre-snapshot-layer comparison point.
- The latest direct `edit-layout` rerun after the panel-icon mask-atlas pass landed at `drag_loop per_iter_ms=2.24`, `snap avg_ms=0.06`, `apply avg_ms=0.05`, and `paint_draw avg_ms=2.11`; the benchmark includes the app-style layout mutation tail and one forced redraw per pointer move.
- The latest direct `theme-change` rerun after the same pass landed at `theme_loop per_iter_ms=4.11`, `dashboard_config avg_ms=0.85`, `theme_preview avg_ms=0.84`, and `theme_paint avg_ms=2.18`.
- The real traced drag in `build\casedash_trace.txt` reported `elapsed_ms=6909.736`, `snap_samples=687`, `apply_samples=687`, but only `paint_total_samples=25`; the measured paint cost was acceptable, but queued `WM_PAINT` delivery was starved by continuous mouse input.
- The latest daemon-backed `edit-layout` capture under `build\profile_benchmark_daemon\requests\29824_11608_6003\` reports `drag_loop per_iter_ms=2.36`, `snap avg_ms=0.07`, `apply avg_ms=0.06`, and `paint_draw avg_ms=2.22`; the string-construction leaves from renderer trace formatting are gone, and the remaining inclusive app weight is in `D2DRenderer::DrawTextBlock`, `D2DRenderer::FillSolidRect`, and `DashboardLayoutEditOverlayRenderer::DrawDottedHighlightRect`.
- The latest direct `edit-layout` rerun after changing the D2D solid-brush cache to palette-id slots landed at `drag_loop per_iter_ms=2.34`, `snap avg_ms=0.07`, `apply avg_ms=0.06`, and `paint_draw avg_ms=2.19`; a preceding noisy run landed at `2.65 ms`/`2.50 ms`, so keep comparing this path through repeat direct runs.
- The latest direct `layout-guide-sheet` run splits generation into `sheet_measure`, `sheet_place`, and `sheet_draw`; it reports `sheet_loop per_iter_ms=116.62`, with `sheet_place avg_ms=75.93` dominating and actual offscreen drawing isolated at `sheet_draw avg_ms=30.14`.
- The latest usable daemon-backed `edit-layout` capture under `build\profile_benchmark_daemon\requests\21425_18089_27400\` keeps the benchmark-process inclusive module weight centered on `d2d1.dll`, `DWrite.dll`, `TextShaping.dll`, `amdxx64.dll`, and `D3D11.dll`; the exported call tree still under-symbolizes most app-owned leaf functions and does not surface a new dominant geometry-builder hotspot inside the benchmark process.
- The latest daemon-backed `edit-layout` timing capture under `build\profile_benchmark_daemon\requests\18269_30044_21338\` reports `drag_loop per_iter_ms=2.54`, `snap avg_ms=0.18`, `apply avg_ms=0.08`, and `paint_draw avg_ms=2.27`; its WPR ETL is present, but the exported text summary and call-tree HTML are empty, so it does not replace the latest usable hotspot attribution above.
- The current uncached capture is mildly collector-bound again on this machine: `TelemetryCollector::UpdateSnapshot()` now lands slightly above repaint in both the direct and daemon-backed runs after restoring the live Gigabyte collection allocation required for real board samples.
- `PDH.DLL` is no longer the dominant steady-state collector hotspot after the AMD GPU path stops redundantly asking both ADLX and PDH for live GPU load and used VRAM on supported hardware.
- The remaining collector cost is now concentrated more heavily in the Gigabyte CLR-backed board provider and the AMD vendor GPU provider, while the Direct2D and DirectWrite frame becomes the larger side of the full benchmark again.
- The fast direct reruns and the daemon-backed capture still agree closely enough that the latest collector reduction is real even though the exported text view still does not fully resolve every app-owned leaf by symbol.

Interpretation:

- Snap-path work is no longer the main limiter after the latest preview-resolve optimization.
- The remaining cost in the benchmarked live window path is now mostly in the Direct2D, DirectWrite, text-shaping, and driver stack rather than in any remaining app-side GDI or GDI+ icon work.
- Snap and apply work are no longer the main limiter on this tree; the benchmark now splits mostly between the real collector path and the HWND-backed Direct2D/DirectWrite frame.
- The direct `update-telemetry` benchmark measures the real collector path instead of a synthetic snapshot-mutation loop, and the current split lands at roughly `2.65 ms` in telemetry update versus `2.23 ms` in repaint on this machine.
- The direct `layout-switch` benchmark remains paint-bound on this machine after restoring incremental renderer style updates: repaint sits around `2.43 ms` of the `3.52 ms` loop while the dialog refresh work stays around `0.23 ms`.
- The direct `layout-guide-sheet` benchmark remains placement-score bound after removing the pathological exhaustive stack-order search: measured callout preparation and offscreen drawing are separate timing buckets, and the remaining cost is mostly leader intersection scoring inside `sheet_place`.
- The direct `edit-layout` benchmark remains paint-bound on this tree after the snapshot/overlay layer split: current reruns land around `drag_loop per_iter_ms=2.36`, `snap avg_ms=0.08`, `apply avg_ms=0.06`, and `paint_draw avg_ms=2.21`, so the remaining measured frame cost sits mostly in Direct2D and DirectWrite layer drawing plus final composition rather than in widget-local layout math.
- Suppressing layout-edit tooltip refresh while a drag is active avoids trace-enabled per-move tooltip work, and immediate drag redraw fixes the real app responsiveness issue that the old benchmark did not expose.
- The current direct `mouse-hover` benchmark benefits from retained snapshot layers: hover hit testing stays around `0.22 ms` per step while repaint drops to about `1.10 ms`.
- Disabling benchmark trace output by constructing a trace without an output stream does not regress the maintained direct benchmark set; the latest repeatable runs remain in the established current-tree range.
- Future hotspot confirmation for this tree should prefer the call-tree HTML or a richer symbolized WPA view instead of the flat text export, because the flat export is now too coarse to attribute the remaining app-side draw cost precisely inside `PDH.DLL`, the board CLR path, the AMD vendor-provider path, and the Direct2D plus DirectWrite stack.

## Kept Optimizations

These changes produced real wins and remain in the codebase:

- Avoid full config copies during snap evaluation by applying preview weights directly in the renderer and resolving layout from there.
- Group snap candidates by widget so one snap search can serve multiple target extents with shared extent evaluation.
- Refactor layout similarity indicator collection to avoid repeated representative scans and reduce per-frame container churn.
- Avoid formatting renderer trace strings while interactive drag tracing suppresses renderer details, so guide-size snap and apply preview passes do not pay for dropped diagnostics text.
- Force active layout-edit drags to redraw on each processed pointer move, and skip tooltip refresh during the active drag so trace-enabled sessions do not spam per-frame tooltip skip markers.
- Keep themed shell-icon refresh out of layout-only edit mutations; the icon is color-driven, so layout weight, font, reorder, and numeric widget edits should not rebuild it on every pointer move.
- Build only the one live gauge usage-fill path that the current metric needs instead of prebuilding every cumulative gauge fill path during each relayout.
- Resolve snap-preview guide probes through an extent-only layout pass that skips widget instantiation, widget layout-state caching, and edit-artifact rebuilds.
- Reuse draw-time text layout results for dynamic text anchors and keep all text-anchor measurement on the renderer's shared DirectWrite layout path.
- Reuse one cached `MetricSource` across successive paints while the resolved `SystemSnapshot` revision stays unchanged, so drag frames reuse smoothed throughput history and formatted metric payloads until telemetry publishes a newer snapshot.
- Fix the title-hover regression introduced during optimization work so card title text highlights correctly again.
- Remove the legacy renderer GDI fallback path and keep both live repaint and screenshot export on the same Direct2D and DirectWrite scene.
- Decode the embedded 8-bit grayscale panel-icon mask atlas through WIC and crop fixed 64 x 64 slots through a target-local Direct2D alpha mask, so the renderer no longer depends on GDI+ for icon resources.
- Cache Direct2D solid brushes by `RenderColorId` palette slot and clear the slots on palette rebuild, so draw code never needs a packed-RGBA brush lookup.
- Keep project-owned render-space geometry, color, stroke, and text-style types across the renderer and widget pipeline instead of passing Win32 `RECT`, `POINT`, `HFONT`, `COLORREF`, or `DT_*` contracts through the hot path.
- Keep renderer style updates incremental so layout-only config changes do not rebuild DirectWrite text formats, palette state, or the panel-icon mask atlas during edit-layout drag apply and layout switching.
- Read presented-FPS GPU Engine 3D usage as raw PDH counter arrays and calculate per-instance percentages from previous/current raw values, so process selection still favors the highest GPU consumer while avoiding the heavier formatted wildcard array path.
- Keep snapshot and overlay layers behind the dashboard layer bitmap pool. The main thread reuses returned layer bitmaps instead of allocating from scratch, and the render thread returns superseded frame-owned layers when active and pending frames are replaced.
- Keep dirty animation frames non-coalesced while batching snapshot and overlay region restores, preparing dirty bounds and sampled animation states together once per frame, and drawing each prepared animation once within its conservative dirty bounds.
- Keep DXGI swap-chain presentation on the simple `Present()` path for animation frames. The renderer already restores dirty regions into retained back buffers, primes every physical flip-chain buffer after retained full redraws, and avoids passing the same regions as DXGI dirty-present metadata because that increases present cost on the measured flip-model path.
- Keep throughput graph max labels in the snapshot layer only; per-frame throughput chart animation draws guides, axes, plot, and leader without repeating DirectWrite label drawing.
- Cache generated layout-edit theme preview triangle pixels by size, DPI, system face color, and resolved theme colors, so repeated preview paints reuse the expensive Oklab per-pixel mix and still redraw live labels and guide outlines.

## Tested Hypotheses

### Hypothesis: Retained histories can avoid a string-key hash index

- Change:
  - Remove `SystemSnapshot`'s `std::unordered_map<std::string, size_t>` retained-history index.
  - Keep retained samples in the dump-facing vector, cache fixed CPU/GPU/network/storage histories by `RetainedHistoryKey` encoded vector indices, and leave dynamic board temperature/fan histories on the existing string series refs.
- Result:
  - Helped shipped size while keeping the retained-history lookup out of the telemetry-plus-draw profile.
- Observed effect:
  - A shared `StringPointerMap`/`StringIndexMap` wrapper around `std::unordered_map<std::string, const void*>` regressed the app to `1,187,328` bytes, so that abstraction was rejected.
  - Removing the retained-history hash index reduced `build\CaseDash.exe` from `1,186,816` to `1,182,208` bytes. A plain vector scan and the final enum-index shape measured the same shipped size; the enum-index shape remains because fixed histories avoid repeated vector scans.
  - Direct validation after the change landed at `update_loop per_iter_ms=5.72`, `telemetry_update avg_ms=3.36`, `paint_draw avg_ms=2.36`, and `edit-layout paint_draw avg_ms=2.33` on a slower-than-baseline run.
  - Daemon-backed captures landed at `update_loop per_iter_ms=5.42` to `5.84`, `telemetry_update avg_ms=3.08` to `3.46`, and `paint_draw avg_ms=2.34` to `2.37`; retained-history lookup was absent from the top app functions in one capture and only `0.12%` exclusive hits in the other.
- Conclusion:
  - Keep enum-indexed fixed retained histories plus dynamic string board histories. Do not add a project-owned unordered-map wrapper for this surface unless a future benchmark shows a larger non-dynamic string-key lookup domain.

### Hypothesis: Keep FPS ETW process caches flat without regressing process-heavy machines

- Change:
  - Keep the FPS ETW provider's active process-name cache, per-process GPU usage totals, and present-event buckets as flat vectors after the size pass.
  - Keep previous/current GPU raw counter lookup on `std::unordered_map<std::wstring, PDH_RAW_COUNTER>` because GPU Engine exposes one instance per process and engine, not one entry per active presenter.
- Result:
  - Helped shipped size without regressing the maintained telemetry-plus-draw benchmark on a process-heavy desktop.
- Observed effect:
  - The validation machine had `325` normal processes, `632` GPU Engine counter instances, and `28` GPU Engine process ids before the stress run.
  - Direct `update-telemetry` reruns landed at `update_loop per_iter_ms=4.77` to `4.84`, `telemetry_update avg_ms=2.66` to `2.67`, and `paint_draw avg_ms=2.11` to `2.17`.
  - With `300` additional hidden idle `timeout.exe` processes alive, the same direct benchmark reported `process_count=927`, `update_loop per_iter_ms=4.78`, `telemetry_update avg_ms=2.63`, and `paint_draw avg_ms=2.14`.
  - The daemon-backed profile under `build\profile_benchmark_daemon\requests\10827_2817_24593\` landed at `update_loop per_iter_ms=4.85`, `telemetry_update avg_ms=2.72`, and `paint_draw avg_ms=2.13`; `PresentedFpsEtwProvider::Sample` stayed at `0.79%` exclusive hits and the raw-counter hash lookup stayed at `0.40%` exclusive hits.
- Conclusion:
  - Keep flat vectors only for the tiny active process-level FPS sets. Keep GPU raw counter lookup hash-based, and do not flatten it for size because the instance count scales with the desktop's process and GPU-engine surface.

### Hypothesis: Use raw PDH arrays for presented-FPS GPU Engine process selection

- Change:
  - Replace `PdhGetFormattedCounterArrayW(PDH_FMT_DOUBLE)` in the local presented-FPS ETW provider's GPU Engine 3D selector with `PdhGetRawCounterArrayW`, keep previous and current raw values per GPU Engine instance, and calculate the percentage with `PdhCalculateCounterFromRawValue`.
  - Seed the previous raw values immediately after the initial GPU Engine PDH collect so the first real sample can still compare GPU Engine usage, and reuse the raw-value maps between samples to avoid rebuilding their storage.
- Result:
  - Helped materially while preserving the requirement that FPS process selection prefers the process with dominant GPU Engine 3D usage.
- Observed effect:
  - Before the change, the reverted-code direct run landed at `update_loop per_iter_ms=5.25`, `telemetry_update avg_ms=3.09`, and `paint_draw avg_ms=2.16`; the daemon-backed profile under `build\profile_benchmark_daemon\requests\24121_8816_21315\` landed at `update_loop per_iter_ms=5.25`, `telemetry_update avg_ms=3.11`, and `paint_draw avg_ms=2.15`, with `PDH.DLL` at about `7.73%` exclusive hits.
  - The final direct run with raw GPU Engine PDH values landed at `update_loop per_iter_ms=4.57`, `telemetry_update avg_ms=2.50`, and `paint_draw avg_ms=2.06`.
  - The final daemon-backed profile under `build\profile_benchmark_daemon\requests\26329_243_6102\` landed at `update_loop per_iter_ms=4.64`, `telemetry_update avg_ms=2.56`, and `paint_draw avg_ms=2.08`, with `PDH.DLL` at about `2.41%` exclusive hits.
- Conclusion:
  - Keep raw GPU Engine PDH sampling for presented-FPS process selection. The benchmark still pays the real synchronous ETW/FPS selector work on every telemetry sample, but avoids the more expensive formatted wildcard array conversion.

### Hypothesis: Cache the presented-FPS GPU Engine cross-check within one telemetry cadence

- Change:
  - Temporarily cached the FPS ETW provider's GPU Engine 3D PDH cross-check for 0.5 seconds while leaving ETW event ingestion and present-event selection active.
- Result:
  - Rejected and reverted because it made the tight-loop benchmark hide cost that the real app pays on each 0.5 second telemetry sample.
- Observed effect:
  - With the cache, `build\CaseDashBenchmarks.exe update-telemetry 240 2` appeared to improve to about `update_loop per_iter_ms=4.14`, `telemetry_update avg_ms=2.10`, and `paint_draw avg_ms=2.04`, and the daemon-backed profile no longer showed `PresentedFpsEtwProvider::Sample` as a top app-inclusive function.
  - After reverting the cache, `build\CaseDashBenchmarks.exe update-telemetry 240 2` landed at `update_loop per_iter_ms=5.25`, `telemetry_update avg_ms=3.09`, and `paint_draw avg_ms=2.16`.
  - The reverted-code daemon-backed profile under `build\profile_benchmark_daemon\requests\24121_8816_21315\` landed at `update_loop per_iter_ms=5.25`, `telemetry_update avg_ms=3.11`, and `paint_draw avg_ms=2.15`; `PresentedFpsEtwProvider::Sample` and `PDH.DLL` are visible again.
- Conclusion:
  - Do not cache or cadence-gate telemetry provider work just to improve `update-telemetry`. This benchmark is intended to measure the synchronous cost of one real telemetry sample plus one draw, so future improvements should reduce the actual per-sample FPS/PDH work or change production behavior deliberately with matching spec updates.

### Hypothesis: Collision-aware container anchors regress layout-edit benchmark paths

- Change:
  - Place container child reorder anchors around occupied widget anchor hit regions, skip empty child slots, and batch dynamic collision resolution once per frame after draw-time dynamic anchors register.
- Result:
  - Did not regress the direct layout-edit drag benchmark, and batching avoided an extra paint cost in mouse-hover.
- Observed effect:
  - `build\CaseDashBenchmarks.exe edit-layout 240 2` reruns landed at `drag_loop per_iter_ms=2.37` to `2.39`, `snap avg_ms=0.18`, `apply avg_ms=0.08`, and `paint_draw avg_ms=2.09` to `2.11`.
  - `build\CaseDashBenchmarks.exe mouse-hover 240 2` landed at `hover_loop per_iter_ms=2.34`, `hover_hit_test avg_ms=0.21`, and `paint_draw avg_ms=2.13`; the confirmation `480`-point run landed at `hover_loop per_iter_ms=2.32`, `hover_hit_test avg_ms=0.21`, and `paint_draw avg_ms=2.12`.
- Conclusion:
  - The collision-aware anchor placement is safe for the drag benchmark and normal repaint cost; the follow-up hover-resolver optimization below addresses the active-region hit-test cost separately.

### Hypothesis: Layout guide sheet generation is slow because callout placement scoring is combinatorial

- Change:
  - Add a `layout-guide-sheet` benchmark mode that generates the selected-layout guide sheet into an in-memory offscreen surface, excluding PNG encoding and file I/O.
  - Replace exhaustive stack-order permutation inside `PlaceLayoutGuideSheetCallouts` with the existing target-Y radial stack order, keep bounded side-split repair for smaller blocks, cache trial target safe rectangles, and reject most segment-safe-rect tests with a bounding-box check before doing segment intersection math.
- Result:
  - Helped materially.
- Observed effect:
  - Before the placement scoring change, `build\CaseDashBenchmarks.exe layout-guide-sheet 1 2` ran at `sheet_loop per_iter_ms=53444.32`; older benchmark builds reported placement and drawing together as `sheet_render`.
  - The daemon-backed profile under `build\profile_benchmark_daemon\requests\31300_11234_18822\` put almost all samples in `PlaceLayoutGuideSheetCallouts`, especially `SegmentIntersectsRect` and `LeaderSegmentsIntersect`.
  - Removing exhaustive stack-order permutation reduced `build\CaseDashBenchmarks.exe layout-guide-sheet 20 2` to about `sheet_loop per_iter_ms=534.93`.
  - Adding safe-rectangle caching and a segment bounding-box reject reduced the same direct benchmark to about `sheet_loop per_iter_ms=193.40`.
  - The confirmation daemon-backed profile under `build\profile_benchmark_daemon\requests\23902_22739_322\` landed at `sheet_loop per_iter_ms=198.19`, with remaining cost still led by placement scoring but with Direct2D/WIC offscreen rendering now visible as the next significant bucket.
  - Disabling global side repair entirely reduced the direct benchmark to about `sheet_loop per_iter_ms=39.92`, but increased leader-intersection warnings and worsened routing quality, so that more aggressive shortcut was rejected.
  - Splitting the benchmark timing showed the current direct `build\CaseDashBenchmarks.exe layout-guide-sheet 3 2` run at `sheet_loop per_iter_ms=328.70`, `sheet_measure avg_ms=4.51`, `sheet_place avg_ms=299.16`, and `sheet_draw avg_ms=16.97`; placement remains the optimization target.
- Conclusion:
  - Layout guide sheet generation should avoid factorial or high-order callout stack permutation. The radial target-Y stack order is the scalable default; future routing improvements should operate on a small set of problematic leaders rather than trying every stack permutation or every side split globally.

### Hypothesis: Hover hit testing regressed because ordinary hover builds diagnostic active-region payloads

- Change:
  - Store container child reorder active-region payloads per child instead of copying the full child-rect vector into every child region.
  - Route `LayoutEditController::ResolveHover()` through a renderer-owned direct hover resolver that scans the already-resolved layout, anchor, guide, and gap collections without constructing diagnostic `LayoutEditActiveRegions`.
  - Keep the generic active-region resolver and trace payloads available for tests, diagnostics, and tools, but avoid that allocation-heavy path for ordinary mouse hover.
- Result:
  - Helped materially and restored hover hit testing close to the maintained pre-regression cost while preserving the new collision-aware anchor behavior.
- Observed effect:
  - The initial daemon-backed hotspot capture `profile_benchmark.cmd mouse-hover 240 2` under `build\profile_benchmark_daemon\requests\6091_2699_22676\` reported `hover_hit_test avg_ms=0.22` and showed app-side time in active-region construction, variant/vector payload churn, `ResolveLayoutEditHover`, and anchor hit testing.
  - Per-child reorder payloads and active-region reservation reduced `profile_benchmark.cmd mouse-hover 240 2` to `hover_hit_test avg_ms=0.14`.
  - The direct renderer hover resolver reduced direct `build\CaseDashBenchmarks.exe mouse-hover 240 2` reruns to `hover_loop per_iter_ms=2.20` to `2.21`, `hover_hit_test avg_ms=0.08`, and `paint_draw avg_ms=2.12` to `2.13`.
  - Daemon-backed confirmation runs under `build\profile_benchmark_daemon\requests\8240_30030_13589\` and `build\profile_benchmark_daemon\requests\8367_23234_22164\` landed at `hover_hit_test avg_ms=0.09` and `0.08`; the remaining profile is dominated by the Direct2D, DirectWrite, and driver paint frame rather than active-region payload construction.
  - A circle-anchor bounding-box guard was neutral within noise, but remains because it avoids needless ring-distance math outside the padded handle bounds.
  - `build\CaseDashBenchmarks.exe edit-layout 240 2` stayed in range at `drag_loop per_iter_ms=2.36` to `2.38`, `snap avg_ms=0.17` to `0.18`, `apply avg_ms=0.08`, and `paint_draw avg_ms=2.09` to `2.11`; daemon-backed `profile_benchmark.cmd edit-layout 240 2` landed at `drag_loop per_iter_ms=2.52`, `snap avg_ms=0.18`, `apply avg_ms=0.08`, and `paint_draw avg_ms=2.24`.
- Conclusion:
  - Ordinary hover should resolve against renderer-owned layout/edit collections directly. `LayoutEditActiveRegions` remain the diagnostic and validation surface, but rebuilding that payload set on every mouse move duplicates already-resolved renderer state and makes hover hit testing pay for trace-friendly structures it does not need.

### Hypothesis: Preserve incremental renderer style updates after the renderer package refactor

- Change:
  - Restore change detection inside `D2DRenderer::SetStyle` so it initializes Direct2D once, rebuilds palette state only when colors change, reloads panel-icon resources only when icon names change, and rebuilds DirectWrite text formats only when fonts or render scale change.
- Result:
  - Helped materially and fixed a refactor regression.
- Observed effect:
  - Before the fix, `build\CaseDashBenchmarks.exe edit-layout 240 2` reruns landed at `drag_loop per_iter_ms=3.34` to `3.35`, `snap avg_ms=0.18`, `apply avg_ms=0.54`, and `paint_draw avg_ms=2.62` to `2.63`.
  - Before the fix, `build\CaseDashBenchmarks.exe layout-switch 240 2` reruns landed at `switch_loop per_iter_ms=4.32` to `4.33`, `switch_apply avg_ms=1.18` to `1.19`, `dialog_refresh avg_ms=0.15` to `0.16`, and `switch_paint avg_ms=2.96` to `2.99`.
  - After the fix, `build\CaseDashBenchmarks.exe edit-layout 240 2` reruns landed at `drag_loop per_iter_ms=2.46` to `2.49`, `snap avg_ms=0.18`, `apply avg_ms=0.08`, and `paint_draw avg_ms=2.20` to `2.23`.
  - After the fix, `build\CaseDashBenchmarks.exe layout-switch 240 2` reruns landed at `switch_loop per_iter_ms=3.60` to `3.66`, `switch_apply avg_ms=0.73` to `0.74`, `dialog_refresh avg_ms=0.15`, and `switch_paint avg_ms=2.72` to `2.76`.
- Conclusion:
  - Layout-only config updates must keep renderer-owned resources hot. Rebuilding DirectWrite formats and panel-icon resources inside every `SetConfig` call dominates the apply phase and shows up immediately in both edit-layout drag and layout switching.

### Hypothesis: Caching the embedded layout-edit template materially improves layout switching while the edit dialog is open

- Change:
  - Add a `layout-switch` benchmark mode to `CaseDashBenchmarks` and `profile_benchmark.cmd`, cycle the configured named layouts through `SelectLayout()` plus `DashboardRenderer::SetConfig()`, rebuild the edit-dialog tree model each iteration, and compare cached versus uncached template-tree rebuild timings while measuring the full switch loop.
- Result:
  - Rejected.
- Observed effect:
  - `build\CaseDashBenchmarks.exe layout-switch 240 2` ran at `switch_loop per_iter_ms=3.63`, `switch_apply avg_ms=0.76`, `dialog_refresh avg_ms=0.15`, `switch_paint avg_ms=2.71`, while the isolated cached and uncached tree rebuild reruns both landed at `0.13 ms` per iteration.
  - `build\CaseDashBenchmarks.exe layout-switch 480 2` reran at `switch_loop per_iter_ms=3.48`, `switch_apply avg_ms=0.74`, `dialog_refresh avg_ms=0.14`, `switch_paint avg_ms=2.59`, while the isolated cached and uncached tree rebuild reruns again both landed at `0.13 ms` per iteration.
- Conclusion:
  - The embedded-template cache does not buy a meaningful steady-state layout-switch win on this machine. The tree rebuild itself is already small, the cached-versus-uncached delta stays inside run-to-run noise, the dominant cost in the benchmarked open-dialog path is repaint rather than template loading, and the app now uses the simpler uncached path.

### Hypothesis: Steady-state telemetry refresh cost is mostly repaint, not snapshot mutation

- Change:
  - Add a second `update-telemetry` benchmark mode to `CaseDashBenchmarks`, thread that selector through `profile_benchmark.cmd`, and measure a loop that constructs the real `TelemetryCollector`, uses the collector-resolved runtime config, calls `TelemetryCollector::UpdateSnapshot()` each iteration, and repaints the collector-owned snapshot without inserting any timer wait.
- Result:
  - Rejected.
- Observed effect:
  - `build\CaseDashBenchmarks.exe update-telemetry 60 2` ran at `update_loop per_iter_ms=12.01`, `telemetry_update avg_ms=9.97`, and `paint_draw avg_ms=2.04`.
  - `build\CaseDashBenchmarks.exe update-telemetry 120 2` ran at `update_loop per_iter_ms=10.90`, `telemetry_update avg_ms=8.90`, and `paint_draw avg_ms=2.00`.
- Conclusion:
  - The earlier repaint-bound result came from a synthetic snapshot-mutation loop and does not describe the real app path. With the real collector in place, steady-state telemetry refresh is collector-bound on this tree, so future `update-telemetry` wins should focus on `TelemetryCollector::UpdateSnapshot()` and its provider work before chasing another millisecond out of repaint.

### Hypothesis: Cache slow-changing collector inputs and narrow the per-refresh network query path

- Change:
  - Reuse board-vendor and GPU-vendor samples for one second, reuse per-drive capacity and free-space metadata for five seconds while still refreshing storage throughput each tick, and replace the per-refresh `GetIfTable2` walk in steady-state network updates with `GetIfEntry2` against the already selected adapter.
- Result:
  - Helped materially, but was backed out.
- Observed effect:
  - Before the change, `build\CaseDashBenchmarks.exe update-telemetry 120 2` ran at `update_loop per_iter_ms=10.08`, `telemetry_update avg_ms=8.06`, and `paint_draw avg_ms=2.02`.
  - After the change, `build\CaseDashBenchmarks.exe update-telemetry 120 2` ran at `update_loop per_iter_ms=4.17`, `telemetry_update avg_ms=2.34`, and `paint_draw avg_ms=1.83`.
  - The follow-up daemon-backed run `profile_benchmark.cmd update-telemetry 240 2` landed at `update_loop per_iter_ms=4.41`, `telemetry_update avg_ms=2.42`, and `paint_draw avg_ms=2.00`.
- Conclusion:
  - The cache-backed version proves those sources are expensive, but the current benchmark intentionally leaves board, GPU-vendor, and drive metadata uncached so steady-state profiles keep stressing the full telemetry path and expose which source is intrinsically slow.

### Hypothesis: Trim telemetry bookkeeping around live API calls without reusing sampled values

- Change:
  - Keep every telemetry source live on every update, but stop building dynamic trace strings when no trace stream is attached, reuse the GPU PDH array scratch buffer, reuse per-drive root-path state instead of rebuilding it per refresh, cache ADLX support flags discovered at initialization, and stop rebuilding verbose provider diagnostics payloads on each successful vendor sample.
- Result:
  - Helped materially.
- Observed effect:
  - Before the change, `build\CaseDashBenchmarks.exe update-telemetry 240 2` ran at `update_loop per_iter_ms=7.75`, `telemetry_update avg_ms=5.58`, and `paint_draw avg_ms=2.18`.
  - After the change, `build\CaseDashBenchmarks.exe update-telemetry 240 2` ran at `update_loop per_iter_ms=6.19`, `telemetry_update avg_ms=4.28`, and `paint_draw avg_ms=1.91`.
  - The follow-up daemon-backed run `profile_benchmark.cmd update-telemetry 240 2` landed at `update_loop per_iter_ms=6.16`, `telemetry_update avg_ms=4.22`, and `paint_draw avg_ms=1.94`.
- Conclusion:
  - The no-cache benchmark still spends most of its time in the real telemetry APIs, but a meaningful slice of the old cost was collector-side scaffolding around those calls. Lazy trace formatting and reusable per-provider scratch state are worth keeping because they reduce benchmark noise without hiding the real source-update cost.

### Hypothesis: Collapse the GPU PDH path into one query collect and one array scan

- Change:
  - Put the GPU dedicated-memory counter onto the same PDH query as the GPU engine-utilization counter and replace the two `PdhGetFormattedCounterArrayW` scans for GPU load with one combined array walk that computes both the 3D-only total and the all-engine total.
- Result:
  - Regressed and was reverted.
- Observed effect:
  - Before the change, `build\CaseDashBenchmarks.exe update-telemetry 240 2` ran at about `update_loop per_iter_ms=6.19`, `telemetry_update avg_ms=4.28`, and `paint_draw avg_ms=1.91`.
  - With the collapsed GPU PDH path in place, the same benchmark regressed to about `update_loop per_iter_ms=7.07`, `telemetry_update avg_ms=5.06`, and `paint_draw avg_ms=2.01`.
- Why it failed:
  - Combining the GPU memory and engine counters onto one PDH query and scanning the shared wildcard arrays together increased the measured hot-path cost instead of reducing it, likely because the merged query shape or the broader array fetch paid more than the saved call count.
- Conclusion:
  - Do not assume fewer PDH calls automatically help. On this tree, the split GPU query shape is faster than the collapsed variant and remains the better live-update baseline.

### Hypothesis: Reuse board-provider reflection scaffolding and remove remaining vendor trace-format churn

- Change:
  - Keep every telemetry source live on every update, but let the Gigabyte SIV reflection call reuse hot-path argument arrays and cached managed unit strings instead of rebuilding them per sample, and make the remaining AMD per-sample metric trace formatting lazy when no trace stream is attached.
- Result:
  - Helped materially, but one attempted sub-change was backed out after real-provider validation.
- Observed effect:
  - Before the change, `build\CaseDashBenchmarks.exe update-telemetry 240 2` ran at about `update_loop per_iter_ms=6.19`, `telemetry_update avg_ms=4.28`, and `paint_draw avg_ms=1.91`.
  - The first version of the change, which also passed the SIV collection parameter as a null by-ref out value, benchmarked at `update_loop per_iter_ms=5.16`, `telemetry_update avg_ms=3.33`, and `paint_draw avg_ms=1.84`, with a confirmation rerun at `update_loop per_iter_ms=5.34`, `telemetry_update avg_ms=3.48`, and `paint_draw avg_ms=1.86`.
  - A fresh real headless validation run on April 18, 2026 with `CaseDash.exe /trace:build\gigabyte_fixed_trace.txt /dump:build\gigabyte_fixed_dump.txt /default-config /exit` showed that null by-ref out value faulted inside SIV as `gigabyte_siv:snapshot_exception ... NullReferenceException`, so the collection-instance part of the optimization was reverted while keeping the reused argument arrays and cached unit strings.
  - With the live collection instance restored, `build\CaseDashBenchmarks.exe update-telemetry 240 2` now runs at `update_loop per_iter_ms=4.05`, `telemetry_update avg_ms=2.19`, and `paint_draw avg_ms=1.86`.
  - A confirmation rerun `build\CaseDashBenchmarks.exe update-telemetry 120 2` lands at `update_loop per_iter_ms=4.14`, `telemetry_update avg_ms=2.22`, and `paint_draw avg_ms=1.92`.
  - The latest daemon-backed run `profile_benchmark.cmd update-telemetry 240 2` lands at `update_loop per_iter_ms=4.28`, `telemetry_update avg_ms=2.29`, and `paint_draw avg_ms=2.00`.
- Conclusion:
  - The live-update benchmark still spends most of its time inside real telemetry APIs, but the Gigabyte provider was paying meaningful extra CPU for per-sample reflection argument setup and managed-string churn. Reusing those internal resources is worth keeping, but `GetCurrentMonitoredData` still requires a live collection instance even on its by-reference parameter, so that specific null-out shortcut must stay reverted.

### Hypothesis: Compute both GPU PDH engine totals from one wildcard fetch

- Change:
  - Keep the GPU engine and dedicated-memory counters on separate PDH queries, but read the engine wildcard array once per update and compute both the 3D-only and all-engine totals from that one buffer instead of calling `PdhGetFormattedCounterArrayW` twice on the same GPU engine counter.
- Result:
  - Helped materially.
- Observed effect:
  - Before the change, `build\CaseDashBenchmarks.exe update-telemetry 240 2` ran at about `update_loop per_iter_ms=5.16`, `telemetry_update avg_ms=3.33`, and `paint_draw avg_ms=1.84`.
  - After the change, the same benchmark ran at `update_loop per_iter_ms=4.43`, `telemetry_update avg_ms=2.59`, and `paint_draw avg_ms=1.84`.
  - A confirmation rerun `build\CaseDashBenchmarks.exe update-telemetry 120 2` landed at `update_loop per_iter_ms=4.68`, `telemetry_update avg_ms=2.82`, and `paint_draw avg_ms=1.87`.
- Conclusion:
  - The earlier failed merged-query experiment did not mean the duplicated GPU engine-array fetch was free. Keeping the memory counter on its own query while collapsing the duplicated engine-array fetch removes real PDH CPU without changing the live data source.

### Hypothesis: Prefer live ADLX GPU usage and VRAM metrics over redundant PDH sampling on supported AMD hardware

- Change:
  - Extend the AMD ADLX provider to return live GPU usage and live used-VRAM metrics alongside temperature, clock, and fan, then let `collector_gpu.cpp` use those vendor metrics first and fall back to the existing PDH load and VRAM paths only when the vendor provider does not supply them.
- Result:
  - Helped materially.
- Observed effect:
  - Before the change, `build\CaseDashBenchmarks.exe update-telemetry 240 2` ran at about `update_loop per_iter_ms=4.43`, `telemetry_update avg_ms=2.59`, and `paint_draw avg_ms=1.84`.
  - After the change, `build\CaseDashBenchmarks.exe update-telemetry 240 2` ran at `update_loop per_iter_ms=3.27`, `telemetry_update avg_ms=1.44`, and `paint_draw avg_ms=1.83`.
  - A confirmation rerun `build\CaseDashBenchmarks.exe update-telemetry 120 2` landed at `update_loop per_iter_ms=3.39`, `telemetry_update avg_ms=1.55`, and `paint_draw avg_ms=1.84`.
  - The follow-up daemon-backed run `profile_benchmark.cmd update-telemetry 240 2` landed at `update_loop per_iter_ms=3.55`, `telemetry_update avg_ms=1.59`, and `paint_draw avg_ms=1.96`.
- Conclusion:
  - The redundant AMD path was asking two different live APIs for the same GPU load and used-VRAM facts every refresh. Preferring ADLX for those supported vendor metrics and keeping PDH only as fallback is the highest-value no-cache win in this workstream, and it pushes telemetry update below repaint on the target machine.

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
  - Keep one renderer-owned `MetricSource` alive across draw calls and invalidate it only when the incoming `SystemSnapshot` revision changes, instead of rebuilding throughput smoothing and per-widget metric caches on every drag repaint.
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
  - `build\CaseDashBenchmarks.exe edit-layout 240 2` ran at `drag_loop per_iter_ms=2.48`, `snap avg_ms=0.20`, `apply avg_ms=0.27`, and `paint_draw avg_ms=2.00`.
  - `build\CaseDashBenchmarks.exe edit-layout 480 2` ran at `drag_loop per_iter_ms=2.45`, `snap avg_ms=0.20`, `apply avg_ms=0.27`, and `paint_draw avg_ms=1.98`.
  - `profile_benchmark.cmd edit-layout 240 2` kept the benchmark hotspot shape in `d2d1.dll`, `DWrite.dll`, `TextShaping.dll`, the display driver, and `win32kfull.sys`, with no new GDI text hotspot.
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

### Hypothesis: Pre-resolve throughput and gauge display payloads inside `MetricSource`

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
  - `build\CaseDashBenchmarks.exe edit-layout 240 2` reruns improved from about `drag_loop per_iter_ms=2.60-2.68`, `snap avg_ms=0.19-0.20`, `apply avg_ms=0.13`, and `paint_draw avg_ms=2.28-2.35` down to about `drag_loop per_iter_ms=2.54-2.57`, `snap avg_ms=0.18-0.19`, `apply avg_ms=0.12`, and `paint_draw avg_ms=2.22-2.27`.
  - `profile_benchmark.cmd edit-layout 240 2` stayed on the same Direct2D, DirectWrite, text-shaping, and driver-stack hotspot shape, with no new app-owned hotspot overtaking the frame.
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
  - Decode embedded PNG panel icons through WIC, originally as cached per-icon sources and later as one fixed 64 x 64 slot atlas, upload render-target-local Direct2D bitmaps on demand, and remove the `gdiplus` link dependency from the app and benchmark targets.
- Result:
  - Neutral for throughput and worth keeping for dependency cleanup.
- Observed effect:
  - `240`-iteration reruns landed at `drag_loop per_iter_ms=2.49` to `2.50`, `snap avg_ms=0.20`, `apply avg_ms=0.28`, and `paint_draw avg_ms=2.01` to `2.02`.
  - A later size pass moved the panel icons to one 8-bit grayscale mask atlas and a target-local alpha mask; direct reruns landed at `edit-layout paint_draw avg_ms=2.11` and `theme-change theme_paint avg_ms=2.18`.
- Why it helped:
  - The benchmark keeps the same Direct2D and DirectWrite hotspot shape while removing the last benchmark-process `GdiPlus.dll` dependency and keeping icon decode, scale, screenshot export, and bitmap upload on one WIC plus Direct2D asset path.
- Conclusion:
  - Keep the WIC-based atlas icon path. Future renderer cleanup can assume panel icons, screenshots, and the live frame all stay off GDI+.

### Hypothesis: Keep widget renderer geometry primitive-only with generic paths and arcs

- Change:
  - Replace widget-facing gauge ring and pill bar renderer helpers with primitive filled paths, stroked arcs, rounded rectangles, and ellipses; move gauge segment construction and pill bar layout into widget code; keep Direct2D conversion generic inside the renderer package.
- Result:
  - Initially regressed the maintained layout-edit draw benchmark, then recovered after switching gauge segments from filled annular paths to widget-owned neutral arc primitives and batching those arcs into one renderer-private D2D path.
- Observed effect:
  - Before this change, the current direct baseline was `drag_loop per_iter_ms=2.41` to `2.48`, `snap avg_ms=0.19` to `0.20`, `apply avg_ms=0.13`, and `paint_draw avg_ms=2.09` to `2.15`.
  - After this change, `build\CaseDashBenchmarks.exe edit-layout 240 2` first landed at `drag_loop per_iter_ms=2.60`, `snap avg_ms=0.18`, `apply avg_ms=0.11`, and `paint_draw avg_ms=2.30`; a confirmation rerun landed at `drag_loop per_iter_ms=2.59`, `snap avg_ms=0.18`, `apply avg_ms=0.11`, and `paint_draw avg_ms=2.29`.
  - `build\CaseDashBenchmarks.exe edit-layout 480 2` landed at `drag_loop per_iter_ms=2.58`, `snap avg_ms=0.19`, `apply avg_ms=0.11`, and `paint_draw avg_ms=2.28`.
  - A small follow-up that keeps common `RenderPath` commands inline and stores arc endpoints in the path command did not recover the regression; reruns landed between `drag_loop per_iter_ms=2.52` and `2.60` and `paint_draw avg_ms=2.25` and `2.31`.
  - The daemon-backed `profile_benchmark.cmd edit-layout 240 2` capture was noisy and reported lost ETW events, while a shorter `profile_benchmark.cmd edit-layout 60 2` capture still pointed the frame cost at the Direct2D, DirectWrite, and driver stack rather than snap, apply, or app-owned helper logic.
  - Replacing gauge filled annular segment paths with widget-owned `RenderArc` geometry and neutral `DrawArc`/`DrawArcs` renderer primitives recovered most of the regression: `build\CaseDashBenchmarks.exe edit-layout 240 2` landed at `drag_loop per_iter_ms=2.43`, `snap avg_ms=0.18`, `apply avg_ms=0.08`, and `paint_draw avg_ms=2.17`; confirmation reruns landed at `drag_loop per_iter_ms=2.47` to `2.48`, `snap avg_ms=0.18`, `apply avg_ms=0.08`, and `paint_draw avg_ms=2.20` to `2.22`.
  - Batching `DrawArcs` through a single renderer-private D2D path geometry instead of one path per arc plus a geometry group recovered the remaining cost: `build\CaseDashBenchmarks.exe edit-layout 240 2` landed at `drag_loop per_iter_ms=2.39` to `2.41`, `snap avg_ms=0.18` to `0.19`, `apply avg_ms=0.08`, and `paint_draw avg_ms=2.13` to `2.14`, while `build\CaseDashBenchmarks.exe edit-layout 480 2` landed at `drag_loop per_iter_ms=2.36`, `snap avg_ms=0.18`, `apply avg_ms=0.08`, and `paint_draw avg_ms=2.10`.
- Conclusion:
  - Keep the primitive-only widget renderer boundary, but avoid representing every widget-specific shape as a filled generic path when a neutral primitive maps to a cheaper renderer operation. Widgets still own gauge and pill bar geometry, while the renderer owns only generic path, arc, rounded-rect, ellipse, and polyline drawing; renderer-private batching is the right place to recover performance without reintroducing widget-specific renderer helpers.

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
  - Keep `DashboardRenderer::SetConfig` on the drag apply path, but only rebuild palette state, the panel-icon mask atlas, DirectWrite text formats, and metric caches when the corresponding config inputs actually change instead of reloading all renderer resources on every layout-weight update.
- Result:
  - Helped materially.
- Observed effect:
  - With the cheaper container outline already in place, reruns before this change landed around `drag_loop per_iter_ms=3.54`, `snap avg_ms=0.20`, `apply avg_ms=0.59`, and `paint_draw avg_ms=2.76`.
  - After narrowing `SetConfig` to layout-relevant work only, repeatable reruns landed at `drag_loop per_iter_ms=2.60` to `2.68`, `snap avg_ms=0.19` to `0.20`, `apply avg_ms=0.13`, and `paint_draw avg_ms=2.28` to `2.35`.
- Conclusion:
  - The regression was not just in draw; the drag path was also paying avoidable per-frame renderer reconfiguration churn. Preserve caches and resource rebuilds across layout-only config updates.

### Hypothesis: Guard dropped renderer trace formatting during guide drag

- Change:
  - Add a cheap renderer-trace guard and wrap layout-resolver trace call sites so interactive guide dragging does not build `renderer:*` strings that `WriteTrace` will discard.
  - Keep the size-oriented flat similarity scan; restoring the older keyed/bucketed similarity code did not beat clean HEAD and cost executable bytes.
  - Retest Direct2D dotted rectangle strokes for the active container outline; this remained worse than the manual filled-dot outline and was reverted.
- Result:
  - Helped the guide-size drag path materially.
- Observed effect:
  - Before the guard, the fresh direct rerun landed at `drag_loop per_iter_ms=2.47`, `snap avg_ms=0.20`, `apply avg_ms=0.09`, and `paint_draw avg_ms=2.17`.
  - After the guard, the final full direct rerun landed at `drag_loop per_iter_ms=2.22`, `snap avg_ms=0.06`, `apply avg_ms=0.05`, and `paint_draw avg_ms=2.09`; a daemon-backed profile landed at `drag_loop per_iter_ms=2.36`, `snap avg_ms=0.07`, `apply avg_ms=0.06`, and `paint_draw avg_ms=2.22`.
  - The dotted-stroke retest regressed the same benchmark to about `drag_loop per_iter_ms=3.10` to `3.27` and `paint_draw avg_ms=2.96` to `3.11`.
- Conclusion:
  - The guide-size regression was not caused by the live similarity indicators needing to compute or draw less; it was avoidable diagnostics-string construction in the preview/apply layout passes while renderer trace output was suppressed. Keep the trace guard and keep the manual filled-dot outline.

### Hypothesis: Force live drag redraw instead of waiting for queued paint

- Change:
  - During active layout-edit drags, skip tooltip refresh work in `InvalidateLayoutEdit` and `InvalidateShell`, then call an immediate redraw after each processed drag pointer move.
  - Keep themed shell-icon refresh out of layout-only edit mutations; the app icon is color-driven and does not change when layout weights, fonts, reorder state, or numeric widget parameters change.
  - Update the edit-layout benchmark harness so drag mutations include renderer sync, layout dirty tracking, and one forced redraw per synthetic pointer move.
- Result:
  - Fixed the real-app lag where drag state advanced quickly but visible feedback updated only occasionally.
- Observed effect:
  - The captured real drag in `build\casedash_trace.txt` lasted `6909.736 ms`, processed `687` snap/apply samples, and recorded only `25` paint samples before the fix.
  - After the fix, live dragging tracks the pointer in the real app. The updated direct benchmark landed at `drag_loop per_iter_ms=2.12` to `2.15`, `snap avg_ms=0.06`, `apply avg_ms=0.05`, and `paint_draw avg_ms=2.00` to `2.02`.
- Conclusion:
  - The user-visible lag was message-pump paint starvation, not a lack of compute or draw capacity. Keep explicit drag redraws in the real app and keep the benchmark aligned with that full drag path.

### Hypothesis: Theme preview construction belongs outside the dialog pane

- Change:
  - Move theme-preview triangle construction and drawing into `src/layout_edit_dialog/theme_preview.*`, replace dialog-path `SetPixel` drawing with a 32-bit DIB transfer, and add a `theme-change` benchmark that rotates all configured themes through the dashboard, edit tree, and theme preview flow.
- Result:
  - Helped maintainability and established a whole-flow timing baseline for theme switching.
- Observed effect:
  - `build\CaseDashBenchmarks.exe theme-change 240 2` landed at `theme_loop per_iter_ms=4.92`, with `dashboard_config avg_ms=1.05`, `edit_tree avg_ms=0.18`, `theme_preview avg_ms=1.00`, and `theme_paint avg_ms=2.61`.
- Conclusion:
  - Keep theme-preview rendering behind the shared module and compare future theme-selector work against the full theme-change loop rather than a standalone triangle microbenchmark.

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

- Keep the benchmark comparison on the same command line shape, such as `build\CaseDashBenchmarks.exe update-telemetry 240 2`, `build\CaseDashBenchmarks.exe edit-layout 240 2`, or `build\CaseDashBenchmarks.exe theme-change 240 2`.
- Use `profile_benchmark.cmd update-telemetry 240 2`, `profile_benchmark.cmd edit-layout 240 2`, or `profile_benchmark.cmd theme-change 240 2` directly for profiling validation; it rebuilds automatically through the daemon workflow.
- If an experiment regresses, revert it and record the result here before finishing.
