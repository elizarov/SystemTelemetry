# Profile Benchmark And Optimization Journal

This document owns benchmark workflow and shared performance research. Machine-specific benchmark ranges, current bottlenecks, and next research directions live in `docs/performance/<machine>.md`.
See also: [docs/build.md](build.md) for build and tooling entrypoints, [docs/optimize_size.md](optimize_size.md) for executable-size research, and [docs/architecture.md](architecture.md) for subsystem structure.

## Purpose

This file keeps the shared optimization journal useful across test machines. It records benchmark commands, cross-machine rules, retained optimization decisions, rejected hypotheses that should not be repeated blindly, and practical guidance for new experiments.

## Telemetry Benchmark Invariant

- `update-telemetry` intentionally stresses the real synchronous provider path back-to-back. Do not add benchmark-only cadence gates, retry delays, cached provider samples, or config variants that skip requested telemetry measurements and then treat the result as an optimization.
- A retained telemetry optimization must reduce CPU work paid by the real app for each 250 ms metrics collection, or it must deliberately change production behavior with the owning spec updated in the same change.
- Overlay configs that remove metrics are useful for isolating bottlenecks, but their results are diagnostic variants only. Keep the default benchmark range on the real requested metric set.

## Performance Documentation Rules

- Machine performance files live under `docs/performance/<machine>.md`.
- The `<machine>` name is a concise lowercase board plus GPU description, such as `gigabyte-x570i-amd-rx6800.md`. Keep it short enough to type from memory.
- Each machine file starts with full board vendor and product, all GPU vendors and product names, system memory size, and GPU dedicated-memory sizes.
- Each machine file then records current known ranges for every maintained benchmark, current known bottlenecks, and further research directions for that hardware.
- Machine files do not keep a journal of past experiments. Replace stale ranges with the current known range once repeated direct runs make the new range credible.
- This shared journal records hypotheses, retained or rejected conclusions, benchmark workflow, and guidance that should travel across machines.
- When a benchmark changes, update the relevant machine file first. Update this journal only when the result changes a shared conclusion, rejects or retains a hypothesis, or adds a recurring pitfall.

## Machine Files

- [Gigabyte X570 I + AMD RX 6800](performance/gigabyte-x570i-amd-rx6800.md) tracks the Gigabyte board plus AMD Radeon desktop baseline.
- [Lenovo LNVNB161216 + Intel UHD + NVIDIA GTX 1650 Ti](performance/lenovo-lnvnb161216-intel-uhd-gtx1650ti.md) tracks the Lenovo laptop baseline with Intel and NVIDIA GPU providers.

## Benchmark Workflow

- Start the elevated daemon once with `profile_benchmark.cmd /daemon-start` when repeated unattended profiling runs are needed.
- Build the benchmark executable with `build.cmd /benchmarks` for direct benchmark runs; normal `build.cmd` and release or validation builds do not build `CaseDashBenchmarks.exe`.
- `CaseDashBenchmarks` accepts the supported benchmark names `animation`, `edit-layout`, `layout-guide-sheet`, `layout-switch`, `lenovo-gamezone`, `mouse-hover`, `snapshot-handoff`, `telemetry-init`, `temperature-sources`, `theme-change`, and `update-telemetry` as the first argument; starting it without arguments prints that list and exits without running a benchmark. `profile_benchmark.cmd` uses the same required benchmark-name argument for profiling runs.
- Direct benchmark runs create a disabled trace object without an output stream, so trace formatting and writes do not affect benchmark timing.
- Treat direct `build\CaseDashBenchmarks.exe <benchmark> <iterations> 2` runs as the repeatable comparison loop and xperf profiles as hotspot validation.
- Treat timing lines printed in the elevated daemon console during `profile_benchmark.cmd` as profiler-instrumented wall-clock numbers, not as the repeatable baseline.
- Daemon-backed and one-shot elevated runs persist benchmark stdout and hotspot summaries in the request directory and replay both in the caller window after the request finishes.
- Use `profile_benchmark.cmd /direct <benchmark> <iterations> 2 [extra benchmark args]` for a clean elevated daemon or one-shot `/elevate` run without xperf when provider access requires elevation but repeatable direct-run timing is the target.
- Profile captures use a minimal xperf CPU sample trace with process and image-load metadata, profile stack walking, process-filtered stack export for `CaseDashBenchmarks.exe`, a `256 MB` circular ETL cap by default, and a compact hotspot summary generated from the filtered call tree.
- Override the circular ETL cap with `PROFILE_BENCHMARK_MAX_FILE_MB=<size>` and the call-tree cutoff with `PROFILE_BENCHMARK_STACK_MIN_HITS=<hits>` when a specific investigation needs a different tradeoff.

## Direct Benchmark Commands

- Measure the animation presenter benchmark with `build\CaseDashBenchmarks.exe animation 240 2`; use `profile_benchmark.cmd animation 2400 2` when capturing CPU profiles so setup noise stays below the per-frame presenter work.
- Measure snapshot handoff with `build\CaseDashBenchmarks.exe snapshot-handoff 20 2`; this benchmark keeps a visible render-thread window presenting on vsync, rebuilds no-overlay snapshot layers, publishes through the live asynchronous handoff, and excludes cadence waits from timing totals.
- Measure layout-edit drag with `build\CaseDashBenchmarks.exe edit-layout 240 2`; the benchmark mirrors the app drag path by applying layout mutations through the app-style config tail, refreshing layout dirty tracking, and forcing one redraw per synthetic pointer move.
- Measure layout guide sheet generation with `build\CaseDashBenchmarks.exe layout-guide-sheet 20 2`; the benchmark renders the selected-layout guide sheet to an in-memory offscreen surface and excludes PNG encoding plus file I/O.
- Measure layout switching with `build\CaseDashBenchmarks.exe layout-switch 240 2`; the benchmark cycles configured layouts, applies the selected layout, refreshes the edit-dialog tree model, and repaints.
- Measure Lenovo GameZone WMI sensor access with `build\CaseDashBenchmarks.exe lenovo-gamezone 5 2` or, for access-controlled Lenovo WMI methods, `profile_benchmark.cmd /direct lenovo-gamezone 5 2` through the elevated daemon. The benchmark prints returned temperature and fan sensor names, values, raw diagnostics, and the per-sample WMI query cost.
- Measure local temperature-source alternatives with `build\CaseDashBenchmarks.exe temperature-sources 1 2` or, for privileged vendor and WMI access, `profile_benchmark.cmd /direct temperature-sources 1 2` through the elevated daemon. The benchmark checks Lenovo GameZone, Lenovo no-argument WMI getters, Windows ACPI and performance thermal-zone WMI, Windows storage temperature WMI, LibreHardwareMonitor and OpenHardwareMonitor WMI namespaces, Intel Level Zero GPU temperature, and NVIDIA NVML GPU temperature, then prints availability, values, diagnostics, and per-source timing.
- Measure mouse hover with `build\CaseDashBenchmarks.exe mouse-hover 240 2`; the benchmark moves the layout-edit cursor path from the dashboard top-left to bottom-right, resolving hover hits and drawing overlay state on every step.
- Measure theme changes with `build\CaseDashBenchmarks.exe theme-change 240 2`; the benchmark rotates through all configured themes and measures config copy, color resolution, dashboard reconfiguration, edit-tree rebuild, theme-preview drawing, and dashboard repaint.
- Measure telemetry initialization with `build\CaseDashBenchmarks.exe telemetry-init 2 2`; the benchmark creates a fresh real synchronous collector on every iteration, forces provider samples to run synchronously during initialization, prints create, initialize, and destroy phase splits, reports the full repeated setup plus teardown as `iteration_loop`, defaults to `2` iterations, and ignores the render-scale placeholder kept for the shared command shape.
- Measure telemetry refresh with `build\CaseDashBenchmarks.exe update-telemetry 240 2`; the benchmark deliberately uses the package-private synchronous collector with synchronous provider samples to measure one real provider collection plus one repaint instead of the production telemetry runtime thread scheduler. For machine-specific provider stress against the executable-side overlay, pass a config path after the render scale, such as `build\CaseDashBenchmarks.exe update-telemetry 2000 2 build\config.ini`.
- When the elevated daemon runs a telemetry benchmark with an overlay config, pass an absolute config path; relative paths resolve from the daemon process working directory.

## Profile Commands

- Capture a benchmark-focused CPU profile with `profile_benchmark.cmd animation 2400 2`, `profile_benchmark.cmd edit-layout 240 2`, `profile_benchmark.cmd layout-switch 240 2`, `profile_benchmark.cmd lenovo-gamezone 5 2`, `profile_benchmark.cmd mouse-hover 240 2`, `profile_benchmark.cmd snapshot-handoff 240 2`, `profile_benchmark.cmd telemetry-init 2 2`, `profile_benchmark.cmd temperature-sources 1 2`, `profile_benchmark.cmd theme-change 240 2`, or `profile_benchmark.cmd update-telemetry 240 2` when a change materially moves that benchmark or when hotspot confirmation is needed.
- Capture a layout-guide-sheet CPU profile with `profile_benchmark.cmd layout-guide-sheet 20 2`.
- Real `/trace-prefixes:profile` runs emit only `profile:timing` summaries every 10 seconds for comparable runtime operation names such as `telemetry_update`, `paint_draw`, `presentation_frame_build`, `snapshot_layer_bitmap`, `snapshot_layer_content`, `presentation_frame_publish`, and `animation_frame`.
- `animation_frame` covers animation sampling and composition work and excludes the live DXGI vsync wait. Use those summaries to check whether direct benchmark timings match the interactive app on the same machine without paying verbose provider-trace overhead.

## Shared Hotspot Model

- Animation and snapshot-handoff work now measure the retained Direct2D/DXGI layer pipeline instead of the old direct-paint path. Expected remaining cost is Direct2D/DXGI draw submission, retained dirty-region restore, timeline sampling, widget animation drawing, and hardware-backed layer bitmap construction.
- `snapshot-handoff` is expected to be frame-build bound. `presentation_frame_publish` should stay near zero unless the mailbox or render-thread handoff changes.
- Layout-edit drag, layout-switch, theme-change, and mouse-hover runs are mostly paint-bound once snap, apply, tree refresh, and hover hit testing stay in their documented low ranges. Remaining samples normally sit in Direct2D, DirectWrite, text shaping, WIC or layer drawing, and the display driver.
- `layout-guide-sheet` is placement-score bound. Offscreen drawing is visible, but the dominant target is still leader routing and intersection scoring inside `sheet_place`.
- `telemetry-init` measures repeated fresh real collector setup and teardown. The benchmark forces provider samples to run synchronously so startup latency stays in the `collector_initialize` split; use `collector_destroy` only to spot provider cleanup churn in the repeated loop.
- On the Lenovo laptop profile machine, direct `telemetry-init` overlays show the default multi-second wall-clock cost comes from requested Lenovo Hardware Scan temperature data, while xperf can make even no-board telemetry initialization look multi-second by magnifying CPU PDH, CLR/JIT, kernel, and filter-driver work. Use direct overlay comparisons for absolute timing and profiling-daemon output for attribution.
- `lenovo-gamezone` measures Lenovo's direct GameZone WMI method path without loading the Hardware Scan LdeApi diagnostics stack. Use it to validate whether model-specific GameZone temperature methods are usable before replacing a Hardware Scan temperature source.
- `temperature-sources` is an elevated direct triage benchmark for temperature-provider discovery, not a retained app workload. Use it before adding a new temperature path so unavailable WMI namespaces, zero-returning firmware methods, and GPU-only temperature APIs are rejected with measured local evidence.
- `update-telemetry` measures real synchronous provider work with synchronous provider samples and can be provider-bound on machines with expensive board or GPU vendor APIs. Do not hide that cost with benchmark-only cadence caches.
- The flat text hotspot export often under-symbolizes app leaves once the dominant work is inside Direct2D, DirectWrite, PDH, provider runtimes, or display drivers. Prefer call-tree HTML or a richer WPA view when the flat summary is too coarse.

## Kept Optimizations

These shared decisions produced useful wins or preserve important benchmark semantics:

- Keep snapshot and overlay layers behind the dashboard layer bitmap pool. The main thread reuses returned layer bitmaps, and the render thread returns superseded frame-owned layers when active and pending frames are replaced.
- Keep dirty animation frames non-coalesced while batching snapshot and overlay region restores, preparing dirty bounds and sampled animation states together once per frame, and drawing each prepared animation once within its conservative dirty bounds.
- Keep DXGI swap-chain presentation on the simple `Present()` path for animation frames. The renderer restores dirty regions into retained back buffers, primes every physical flip-chain buffer after retained full redraws, and avoids DXGI dirty-present metadata because it increased present cost on the measured flip-model path.
- Keep throughput graph max labels in the snapshot layer only; per-frame throughput chart animation draws guides, axes, plot, and leader without repeating DirectWrite label drawing.
- Keep the live repaint and screenshot export on the same Direct2D and DirectWrite scene. Do not reintroduce the legacy GDI fallback renderer path.
- Keep the WIC-based panel-icon mask atlas path. Panel icons, screenshots, and the live frame stay off GDI+.
- Keep project-owned render-space geometry, color, stroke, and text-style types across the renderer and widget pipeline instead of passing Win32 `RECT`, `POINT`, `HFONT`, `COLORREF`, or `DT_*` contracts through the hot path.
- Keep renderer style updates incremental so layout-only config changes do not rebuild DirectWrite text formats, palette state, or the panel-icon mask atlas during edit-layout drag apply and layout switching.
- Keep generated layout-edit theme preview triangle pixels cached by size, DPI, system face color, and resolved theme colors while still redrawing live labels and guide outlines.
- Keep active layout-edit drags redrawing from processed pointer messages, and keep tooltip refresh suppressed during active drags so pointer input does not starve visible feedback behind queued `WM_PAINT`.
- Keep the direct renderer hover resolver for ordinary mouse hover. `LayoutEditActiveRegions` remain the diagnostics and validation surface, but ordinary hover should not rebuild trace-friendly payloads on every mouse move.
- Keep the layout guide sheet scorer away from exhaustive stack-order permutation. Future routing improvements should operate on small sets of problematic leaders rather than global combinatorial search.
- Keep raw GPU Engine PDH sampling for presented-FPS process selection and keep the GPU raw-counter lookup hash-based; that domain scales with process and engine count.
- Keep fixed retained-history indexes for fixed CPU, GPU, network, and storage series while leaving dynamic board temperature and fan histories on string series refs.
- Keep telemetry-provider hot-path scaffolding lean without reusing sampled values: lazy trace formatting, reusable provider scratch state, cached support flags discovered at initialization, and compact success diagnostics.
- Keep the Gigabyte SIV provider on the typed fan and temperature query shape with lean per-row extraction.
- Keep AMD ADLX load and used-VRAM metrics preferred over redundant PDH sampling when the selected AMD provider exposes those values.
- Keep widget renderer geometry primitive-only, but prefer neutral renderer primitives such as arcs over generic filled paths when the primitive maps to a cheaper Direct2D operation.

## Research Journal

### Hypothesis: Reuse a throughput chart render-point scratch buffer during animation

- Result: Rejected and reverted.
- Evidence: Direct `animation` reruns stayed neutral to worse after the 250 ms cadence and 120-sample retained-history change.
- Conclusion: Keep the simpler chart point construction until a profile shows larger app-side point-building cost. Current isolated animation cost is dominated by Direct2D/DXGI submission and drawing work.

### Hypothesis: Batch Direct2D polyline geometry points through `AddLines`

- Result: Rejected and reverted.
- Evidence: Direct `animation` reruns stayed in the slower range on the longer 120-sample throughput charts.
- Conclusion: Keep the simple per-point geometry sink path because chart-animation cost is dominated by Direct2D/DXGI draw and present work.

### Hypothesis: Retained histories can avoid a string-key hash index

- Result: Kept.
- Evidence: Removing the retained-history hash index reduced shipped executable size without surfacing retained-history lookup as a top telemetry-plus-draw hotspot.
- Conclusion: Keep enum-indexed fixed retained histories plus dynamic string board histories. Do not add a project-owned unordered-map wrapper unless a future benchmark shows a larger fixed string-key lookup domain.

### Hypothesis: Keep FPS ETW process caches flat without regressing process-heavy machines

- Result: Kept with a boundary.
- Evidence: Flat active process-level FPS sets stayed healthy on a process-heavy desktop, but GPU Engine raw counter instances were large enough to keep hash lookup.
- Conclusion: Keep flat vectors only for tiny active process-level FPS sets. Keep GPU raw counter lookup hash-based.

### Hypothesis: Use raw PDH arrays for presented-FPS GPU Engine process selection

- Result: Kept.
- Evidence: Raw GPU Engine PDH values materially reduced the telemetry-refresh cost while preserving dominant GPU Engine 3D process selection.
- Conclusion: Keep raw GPU Engine PDH sampling and calculate percentages from previous/current raw values.

### Hypothesis: Cache the presented-FPS GPU Engine cross-check within one telemetry cadence

- Result: Rejected and reverted.
- Evidence: The tight-loop benchmark improved only because it hid real provider work that production pays on each telemetry sample.
- Conclusion: Do not cache or cadence-gate telemetry provider work solely to improve `update-telemetry`. Change production behavior deliberately and update behavior docs if cadence changes.

### Hypothesis: Collision-aware container anchors regress layout-edit benchmark paths

- Result: Kept.
- Evidence: Collision-aware anchor placement did not regress the direct layout-edit drag benchmark, and batching avoided extra mouse-hover paint cost.
- Conclusion: Container child reorder anchors can stay collision-aware; ordinary hover uses the direct resolver to avoid diagnostic payload churn.

### Hypothesis: Layout guide sheet generation is slow because callout placement scoring is combinatorial

- Result: Kept with limits.
- Evidence: Removing exhaustive stack-order permutation and adding safe-rectangle plus bounding-box rejects reduced guide-sheet cost by orders of magnitude. Disabling global side repair was much faster but hurt routing quality and stayed rejected.
- Conclusion: Keep scalable radial target-Y stack ordering. Improve only local routing failures rather than trying every stack permutation or side split globally.

### Hypothesis: Hover hit testing regressed because ordinary hover builds diagnostic active-region payloads

- Result: Kept.
- Evidence: Direct renderer hover resolving reduced hover hit testing back near the pre-regression range while preserving diagnostic active-region output for tests and tools.
- Conclusion: Ordinary hover resolves against renderer-owned layout/edit collections directly.

### Hypothesis: Preserve incremental renderer style updates after renderer package refactors

- Result: Kept.
- Evidence: Restoring change detection in `D2DRenderer::SetStyle` removed refactor regressions in edit-layout drag and layout switching.
- Conclusion: Layout-only config updates must keep renderer-owned resources hot.

### Hypothesis: Caching the embedded layout-edit template materially improves layout switching

- Result: Rejected.
- Evidence: Cached and uncached tree rebuild timings stayed inside run-to-run noise, while repaint dominated the full layout-switch loop.
- Conclusion: Keep the simpler uncached embedded-template path unless a future dialog change makes tree rebuild cost visible.

### Hypothesis: Steady-state telemetry refresh cost is mostly repaint, not snapshot mutation

- Result: Rejected as a benchmark assumption.
- Evidence: The real synchronous collector benchmark showed provider collection can dominate telemetry refresh; the older repaint-bound result came from a synthetic snapshot-mutation loop.
- Conclusion: Keep `update-telemetry` on the real collector path so it exposes provider cost.

### Hypothesis: Cache slow-changing collector inputs and narrow the per-refresh network query path

- Result: Proved source cost but was backed out.
- Evidence: Reusing board, GPU-vendor, and drive metadata samples made `update-telemetry` much faster, but it changed benchmark semantics by hiding real source-update cost.
- Conclusion: Do not cache provider samples only to improve the benchmark. Keep every telemetry source live unless production behavior deliberately changes.

### Hypothesis: Trim telemetry bookkeeping around live API calls without reusing sampled values

- Result: Kept.
- Evidence: Lazy trace formatting, reusable scratch buffers, cached support flags, and lean diagnostics reduced collector-side overhead while still sampling live provider data every update.
- Conclusion: Reduce scaffolding around real API calls before considering sample reuse.

### Hypothesis: Collapse the GPU PDH path into one query collect and one array scan

- Result: Rejected and reverted.
- Evidence: Combining GPU memory and engine counters onto one PDH query regressed telemetry refresh despite reducing apparent call count.
- Conclusion: Do not assume fewer PDH calls help. Keep the GPU memory counter on its own query.

### Hypothesis: Reuse board-provider reflection scaffolding and remove vendor trace-format churn

- Result: Partially kept.
- Evidence: Reused argument arrays and cached managed unit strings helped the Gigabyte SIV path. Passing a null by-reference collection output faulted inside SIV and was reverted after real-provider validation.
- Conclusion: Reuse reflection scaffolding, but keep the live collection instance required by `GetCurrentMonitoredData`.

### Hypothesis: Trim Gigabyte SIV typed-sample row processing without reducing samples

- Result: Kept.
- Evidence: Lean typed fan and temperature row extraction reduced telemetry refresh while preserving real SIV samples in diagnostics.
- Conclusion: Keep the typed two-call SIV shape. Do not use the all-sensor overload, sample reuse, persistent collection reuse, or constructor caching for this provider path.

### Hypothesis: Compute both GPU PDH engine totals from one wildcard fetch

- Result: Kept.
- Evidence: Reading the GPU engine wildcard array once and computing both 3D-only and all-engine totals reduced telemetry refresh without merging the memory counter query.
- Conclusion: Keep the split memory query plus single engine-array fetch shape.

### Hypothesis: Prefer live ADLX GPU usage and VRAM metrics over redundant PDH sampling

- Result: Kept.
- Evidence: On supported AMD hardware, ADLX already exposes live GPU load and used VRAM, making matching PDH sampling redundant.
- Conclusion: Prefer selected vendor-provider values when they are available and keep PDH as fallback.

### Hypothesis: Keep widget renderer geometry primitive-only with generic paths and arcs

- Result: Kept after follow-up recovery.
- Evidence: A broad primitive-only boundary initially regressed gauge-heavy repaint until gauge segments moved from generic filled paths to widget-owned neutral arc primitives batched by the renderer.
- Conclusion: Widgets own gauge and pill geometry, while the renderer owns generic paths, arcs, rounded rects, ellipses, and polylines. Use cheap neutral primitives when they preserve the same pixels.

### Hypothesis: Container guide outline feedback needs a cheaper stroke

- Result: Kept.
- Evidence: The dashed Direct2D container outline was the dominant feedback regression. A manual filled-dot outline kept the feedback with much lower drag repaint cost.
- Conclusion: Prefer the manual-dot outline for large container highlights.

### Hypothesis: Guard dropped renderer trace formatting during guide drag

- Result: Kept.
- Evidence: Trace string construction in preview/apply layout passes was measurable even when renderer trace output was suppressed.
- Conclusion: Keep cheap trace guards around hot resolver and renderer trace sites.

### Hypothesis: Theme preview construction belongs outside the dialog pane

- Result: Kept.
- Evidence: Moving preview construction into `layout_edit_dialog/theme_preview.*` and replacing `SetPixel` with a DIB transfer improved maintainability and established the full `theme-change` benchmark.
- Conclusion: Keep theme preview rendering behind the shared module and compare future theme-selector work against the whole-flow benchmark.

### Hypothesis: NVIDIA laptop telemetry should not poll NVML utilization or clock at 250 ms

- Change:
  - Stop using `nvmlDeviceGetUtilizationRates` for NVIDIA load and let the existing Windows GPU Engine counter path provide per-update load.
  - Stop using `nvmlDeviceGetClockInfo` for NVIDIA current clock and use NVAPI current-clock reads instead, treating `NVAPI_GPU_NOT_POWERED` as an unavailable clock for that sample.
  - Add an optional config path argument to `update-telemetry` so local provider stress can run against `build\config.ini`.
- Result:
  - Fixed the real NVIDIA laptop telemetry stalls without caching whole GPU samples.
- Observed effect:
  - The failing real trace showed `nvmlDeviceGetUtilizationRates` returning `Unknown Error` and later showed `nvmlDeviceGetClockInfo` independently blocking for about `390 ms` at the production 250 ms cadence while the dGPU was idle.
  - `nvidia-smi --query-gpu=timestamp,pstate,utilization.gpu,temperature.gpu,clocks.gr,memory.used,memory.total --format=csv -lms 250` reproduced the driver behavior outside CaseDash: idle samples intermittently reported `[Unknown Error]` for P-state and utilization while temperature, clock, and memory remained printable.
  - `build\CaseDashBenchmarks.exe update-telemetry 2000 2 build\config.ini` landed at `update_loop per_iter_ms=7.36`, `telemetry_update avg_ms=2.96`, and `paint_draw avg_ms=4.40`.
  - A verbose real-config trace recorded `43` NVIDIA sample starts and `43` successful sample completions, with zero NVML utilization calls, zero NVML clock calls, zero cached samples, `43` NVAPI clock attempts, `43` successful temperature reads, `43` successful memory reads, and `42` GPU Engine load samples. Runtime `telemetry_update` averaged `5.90 ms` over the main 10-second window.
- Conclusion:
  - On WDDM laptop dGPUs, the idle power state can make NVML utilization and current-clock polling unreliable at the app cadence. Keep NVIDIA load on PDH and current clock on NVAPI so a powered-off dGPU reports clock unavailable quickly instead of blocking the telemetry worker.

## Practical Guidance For Future Experiments

- Keep benchmark comparisons on the same command shape, same iteration count, same warmup count, and same machine file.
- Run repeated direct benchmarks before treating a move as real; use the machine file's current known range rather than a single best or worst run.
- Profile only after a direct benchmark shows a repeatable move or hotspot attribution is needed.
- Speed optimizations that replace `std::unordered_map` with a custom hash-based table keep the table in a separate owning `.h`/`.cpp` module.
- Prioritize experiments that reduce primitive count, switch to a cheaper primitive family, reduce DirectWrite/text-shaping work, or reduce real provider API overhead without hiding live samples.
- Favor richer WPA views or call-tree HTML when flat hotspot summaries hide app-side work behind Direct2D, DirectWrite, provider runtimes, or drivers.
- Rejected experiments can be retried only when surrounding code changed enough to make the old result stale; record the reason in the new hypothesis entry.

## Validation Notes

- Use the smallest direct benchmark loop that proves an experiment while iterating.
- Use `profile_benchmark.cmd` only for profiling validation or elevated provider confirmation.
- If an experiment regresses, revert it and record the result here before finishing.
- When a new result becomes the current hardware baseline, update the matching `docs/performance/<machine>.md` file instead of adding another baseline block here.
