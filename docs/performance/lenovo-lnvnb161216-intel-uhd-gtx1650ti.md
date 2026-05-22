# Lenovo LNVNB161216 + Intel UHD + NVIDIA GTX 1650 Ti Performance

This file records current benchmark ranges, bottlenecks, and next research directions for this test machine. It intentionally does not keep a journal of past experiments.

## Machine Details

- Board: LENOVO LNVNB161216, version `SDK0J40697 WIN`, system model `20V3`.
- CPU: Intel Core i7-10750H, `6` cores and `12` logical processors.
- GPUs: Intel UHD Graphics, DXGI dedicated memory about `0.12 GiB`; NVIDIA GeForce GTX 1650 Ti with Max-Q Design, DXGI dedicated memory about `3.84 GiB`.
- System memory: `15.87 GiB` visible, installed as `2 x 8 GB` Samsung `M471A1K43DB1-CWE` at `2933 MT/s`.
- Hardware providers used by current traces: Lenovo Hardware Scan and GameZone WMI for board telemetry, Intel Level Zero for the default selected GPU, NVIDIA NVML/NVAPI when the NVIDIA adapter is selected, and Presented FPS ETW when `gpu.fps` is requested and elevated or service access is available.

## Current Benchmark Ranges

Ranges use direct `build\CaseDashBenchmarks.exe` runs unless a note says otherwise. Elevated telemetry ranges use `profile_benchmark.cmd /direct ...` through the elevated benchmark daemon because this machine requires elevation for vendor telemetry.

| Benchmark | Current known range | Important split |
| --- | --- | --- |
| `animation 240 2` | `animation_loop` and `animation_frame` about `0.65-1.53 ms` | Current direct reruns report `snapshot_animations=28`, `overlay_animations=0`, and `active_chunk_frames=120`. |
| `snapshot-handoff 20 2` | `snapshot_loop` about `8.3-10.6 ms` | `presentation_frame_build` carries nearly all cost; `presentation_frame_publish` stays near `0.00-0.01 ms`. |
| `edit-layout 240 2` | `drag_loop` about `3.63-4.17 ms` | `snap` about `0.12-0.14 ms`, `apply` about `0.09-0.11 ms`, `paint_draw` about `3.38-3.89 ms`. |
| `layout-guide-sheet 20 2` | `sheet_loop` about `192-223 ms` | `sheet_place` is dominant at about `100-118 ms`; `sheet_draw` is about `58-63 ms`. |
| `layout-switch 240 2` | `switch_loop` about `8.89-11.78 ms` | `switch_apply` about `1.48-2.04 ms`, `dialog_refresh` about `0.33-0.43 ms`, `switch_paint` about `7.03-9.26 ms`. |
| `mouse-hover 240 2` | `hover_loop` about `1.50-1.53 ms` | `hover_hit_test` about `0.38-0.44 ms`; repaint is about `1.07-1.13 ms`. |
| `theme-change 240 2` | `theme_loop` about `5.38-6.73 ms` | `dashboard_config` about `1.25-1.57 ms`, `theme_preview` about `0.44-0.48 ms`, `theme_paint` about `3.26-4.16 ms`. |
| `telemetry-init 1 2`, synchronous provider samples | `iteration_loop` about `8.7-15.0 s` | `collector_initialize` carries nearly all cost; `collector_destroy` stays about `1-4 ms` after synchronous provider sampling keeps Lenovo work out of teardown. |
| `update-telemetry 20 2`, non-elevated default with synchronous provider samples | `update_loop` about `41.9 ms` | `telemetry_update` about `38.3 ms`, `paint_draw` about `3.6 ms`; initialization is not included in the loop line. |
| `update-telemetry 20 2`, profiled elevated default Intel GPU with synchronous provider samples | `update_loop` about `61.0 ms` under xperf | `telemetry_update` about `57.4 ms`, `paint_draw` about `3.6 ms`; the profile still shows initialization samples, so treat this as hotspot attribution rather than a repeatable direct baseline. |
| Elevated no-board and NVIDIA `update-telemetry` variants | Needs refresh | Previous ranges predate synchronous provider samples in the benchmark and are no longer comparable. |

## Current Bottlenecks

- Elevated telemetry remains Lenovo board-provider bound, but the no-board and NVIDIA overlay variants need fresh baselines after the benchmark switched to synchronous provider samples.
- The expensive Lenovo shape is the Hardware Scan temperature path plus GameZone WMI fan fallback. Do not hide that cost with benchmark-only retry delays or provider-sample caches; any fix needs to reduce the real per-sample Hardware Scan or WMI work.
- Presented FPS is not the dominant default elevated bottleneck on this machine. The provider is now demand-driven by the runtime config, so layouts that omit `gpu.fps` do not start the service/local ETW Presented FPS path.
- Intel Level Zero is cheap compared with Lenovo board collection once board rows are removed, but its exact current range needs refresh under synchronous provider samples.
- Telemetry initialization is provider-shaped. With benchmark synchronous provider samples enabled, `collector_initialize` takes about `8.7-15.0 s`; xperf attributes the CPU samples to CLR/JIT, Lenovo certificate validation through WinTrust and crypto modules, CPU PDH counter setup, and Intel Level Zero/GPU adapter selection under `RealTelemetryCollector::Initialize`.
- Render-side benchmarks are slower than the Gigabyte desktop baseline. Direct2D, DirectWrite, text shaping, and the Intel/NVIDIA laptop display-driver stack dominate repaint-heavy paths after app-side hit testing, snap, apply, and config work stay small.
- Daemon-backed config overlays must use absolute paths. Relative overlay paths can resolve from the elevated daemon working directory and silently fall back to the embedded config.

## Further Research Directions

- For Lenovo telemetry, compare default elevated direct runs and `telemetry-init` against the same branch with `CashDashService` installed and running. If service-backed board samples collapse dashboard-process initialization or refresh cost, keep Lenovo Hardware Scan out of the dashboard process whenever possible.
- If direct elevated Lenovo board collection must remain, investigate moving direct Hardware Scan capture into a helper process or reducing the per-sample Hardware Scan/WMI work. The current in-process diagnostics stack is too expensive for benchmark bursts and likely causes visible startup noise.
- Add finer provider-phase timing for `UpdateBoardMetrics`, `UpdateGpuMetrics`, and PDH collectors when the flat xperf summary is too coarse; the current call-tree export shows kernel, PDH, COM, CLR, Direct2D, and DirectWrite time but under-symbolizes the vendor leaves.
- Use absolute, ASCII/no-BOM overlay files for elevated daemon benchmark variants until config parsing grows BOM-tolerant section handling.
- Recheck layout-guide-sheet placement after any routing changes; this laptop is a good stress case for high-placement and high-draw costs.
