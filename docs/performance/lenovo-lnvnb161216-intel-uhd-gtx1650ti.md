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
| `update-telemetry 240 2`, non-elevated default | `update_loop` about `5.54-5.82 ms` | `telemetry_update` about `2.53-2.63 ms`, `paint_draw` about `3.01-3.18 ms`; provider paths that require elevation report unavailable quickly. |
| `update-telemetry 240 2`, elevated default Intel GPU | `update_loop` about `21.1-25.6 ms` | `telemetry_update` about `17.5-21.2 ms`, `paint_draw` about `3.6-4.4 ms`; Lenovo board sensors are the dominant added cost. |
| `update-telemetry 240 2`, elevated no board sensor rows | `update_loop` about `3.93-5.17 ms` | `telemetry_update` about `1.28-1.55 ms`, `paint_draw` about `2.65-3.62 ms`; this diagnostic variant isolates the provider baseline without Lenovo temperature or fan requests. |
| `update-telemetry 240 2`, elevated NVIDIA selected without board sensor rows | `update_loop` about `5.25 ms` | `telemetry_update` about `2.33 ms`, `paint_draw` about `2.92 ms`; NVIDIA NVML/NVAPI is modest when Lenovo board collection is absent. |
| `update-telemetry 240 2`, elevated NVIDIA selected with default board rows | `update_loop` about `31.0 ms` | `telemetry_update` about `25.6 ms`, `paint_draw` about `5.4 ms`; board collection plus dGPU provider work is the slowest current shape. |

## Current Bottlenecks

- Elevated telemetry is Lenovo board-provider bound. Removing board temperature and fan rows drops elevated `telemetry_update` from roughly `17.5-23 ms` to `1.3-1.6 ms`, while removing only `gpu.fps` does not materially change the default board-heavy result.
- The expensive Lenovo shape is the Hardware Scan temperature path plus GameZone WMI fan fallback. Do not hide that cost with benchmark-only retry delays or provider-sample caches; any fix needs to reduce the real per-sample Hardware Scan or WMI work.
- Presented FPS is not the dominant default elevated bottleneck on this machine. The provider is now demand-driven by the runtime config, so layouts that omit `gpu.fps` do not start the service/local ETW Presented FPS path.
- Intel Level Zero is cheap enough once Lenovo board rows are removed. Selecting the NVIDIA adapter without board rows raises telemetry from about `1.3-1.5 ms` to about `2.3 ms`, mostly from NVML/NVAPI provider work.
- Initialization is visibly provider-shaped. The non-elevated trace initializes CPU PDH and adapter/provider selection in hundreds of milliseconds, while elevated Lenovo runs additionally load or query the Lenovo diagnostics stack. Keeping the CaseDash service installed and running should move Lenovo board sampling out of the dashboard process.
- Render-side benchmarks are slower than the Gigabyte desktop baseline. Direct2D, DirectWrite, text shaping, and the Intel/NVIDIA laptop display-driver stack dominate repaint-heavy paths after app-side hit testing, snap, apply, and config work stay small.
- Daemon-backed config overlays must use absolute paths. Relative overlay paths can resolve from the elevated daemon working directory and silently fall back to the embedded config.

## Further Research Directions

- For Lenovo telemetry, compare default elevated direct runs against the same branch with `CashDashService` installed and running. If service-backed board samples collapse the dashboard-process cost, keep Lenovo Hardware Scan out of the dashboard process whenever possible.
- If direct elevated Lenovo board collection must remain, investigate moving direct Hardware Scan capture into a helper process or reducing the per-sample Hardware Scan/WMI work. The current in-process diagnostics stack is too expensive for benchmark bursts and likely causes visible startup noise.
- Add finer provider-phase timing for `UpdateBoardMetrics`, `UpdateGpuMetrics`, and PDH collectors when the flat xperf summary is too coarse; the current call-tree export shows kernel, PDH, COM, CLR, Direct2D, and DirectWrite time but under-symbolizes the vendor leaves.
- Use absolute, ASCII/no-BOM overlay files for elevated daemon benchmark variants until config parsing grows BOM-tolerant section handling.
- Recheck layout-guide-sheet placement after any routing changes; this laptop is a good stress case for high-placement and high-draw costs.
