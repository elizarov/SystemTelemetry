# MSI Z690 Carbon WiFi + NVIDIA RTX 3080 Performance

This file records current benchmark ranges, bottlenecks, and next research directions for this test machine. It intentionally does not keep a journal of past experiments.

## Machine Details

- Board: Micro-Star International Co., Ltd. MPG Z690 CARBON WIFI (MS-7D30), version `1.0`, system model `MS-7D30`.
- CPU: 12th Gen Intel Core i7-12700KF, `12` cores and `20` logical processors.
- GPUs: NVIDIA GeForce RTX 3080, DXGI dedicated memory about `9.82 GiB`, `10240 MiB` reported by `nvidia-smi`, nominal `10 GB`.
- System memory: `63.86 GiB` visible, installed as `2 x 32 GB` Kingston `KF556C40-32` at `5600 MT/s`.
- Hardware providers used by current traces: MSI Center SDK for board CPU temperature plus CPU, system, and GPU fan RPM, NVIDIA NVML/NVAPI for selected GPU telemetry, Windows GPU Engine PDH for NVIDIA load, and Presented FPS ETW when elevation or service access is available.

## Current Benchmark Ranges

Ranges use direct `build\CaseDashBenchmarks.exe` runs from a fresh `build.cmd /benchmarks` build. The first ranges are from three direct passes on this machine.

| Benchmark | Current known range | Important split |
| --- | --- | --- |
| `animation 240 2` | `animation_loop` and `animation_frame` about `0.19-0.22 ms` | Current direct reruns report `snapshot_animations=28`, `overlay_animations=0`, and `active_chunk_frames=120`. |
| `snapshot-handoff 20 2` | `snapshot_loop` about `9.24-9.49 ms` | `presentation_frame_build` carries nearly all cost at about `9.24-9.49 ms`; `presentation_frame_publish` stays near `0.00 ms`. |
| `edit-layout 240 2` | `drag_loop` about `1.71-1.74 ms` | `snap` about `0.09-0.11 ms`, `apply` about `0.06 ms`, `paint_draw` about `1.54-1.57 ms`. |
| `layout-guide-sheet 20 2` | `sheet_loop` about `113-116 ms` | `sheet_place` is about `66.4-67.5 ms`, `sheet_draw` about `28.1-29.1 ms`, `sheet_measure` about `8.61-8.88 ms`, and `active_regions` about `8.90-9.14 ms`. |
| `layout-switch 240 2` | `switch_loop` about `3.75-4.01 ms` | `switch_apply` about `0.70-0.72 ms`, `dialog_refresh` about `0.19-0.20 ms`, `switch_paint` about `2.84-3.08 ms`. |
| `mouse-hover 240 2` | `hover_loop` about `0.44-0.45 ms` | `hover_hit_test` about `0.14-0.15 ms`; repaint is about `0.29-0.30 ms`. |
| `theme-change 240 2` | `theme_loop` about `2.75-2.93 ms` | `dashboard_config` about `0.67-0.69 ms`, `theme_preview` about `0.20-0.22 ms`, `theme_paint` about `1.61-1.74 ms`. |
| `telemetry-init 2 2`, default layout with synchronous provider samples | `iteration_loop` about `370-404 ms` | `collector_initialize` about `368-402 ms`; default adapter selection uses NVIDIA NVML/NVAPI and the default layout requests MSI board-backed temperature and fan metrics. |
| `update-telemetry 240 2`, default layout with synchronous provider samples | `update_loop` about `56.0-63.7 ms` | `telemetry_update` about `54.2-61.9 ms`, `paint_draw` about `1.75-1.78 ms`; initialization is not included in the loop line. |

## Current Bottlenecks

- `update-telemetry` is strongly provider-bound on this machine. The real synchronous collector path spends almost all loop time in telemetry collection, while repaint remains below `2 ms`.
- `telemetry-init` is provider-shaped. The default layout initializes MSI Center SDK board telemetry and NVIDIA NVML/NVAPI GPU telemetry synchronously before the first benchmark sample.
- `layout-guide-sheet` remains placement-score bound. Leader routing and intersection scoring are the largest split, with offscreen drawing still visible but smaller.
- `edit-layout`, `layout-switch`, `theme-change`, and `mouse-hover` are mainly repaint-bound after snap, apply, hover hit testing, config copy, and edit-tree refresh stay small.
- `animation` and `snapshot-handoff` are Direct2D/DXGI layer-pipeline work. Snapshot handoff is frame-build bound and publish cost stays negligible.
- Local Presented FPS ETW is unavailable in the normal unelevated diagnostic trace on this machine with `Access is denied`; use elevation or `CashDashService` when validating `gpu.fps` behavior rather than treating that diagnostic state as a benchmark regression.

## Further Research Directions

- Profile `update-telemetry` before changing provider code on this machine because MSI Center SDK, NVIDIA NVML/NVAPI, GPU Engine PDH, and FPS access can all contribute to the provider-bound shape.
- Compare elevated or service-backed `update-telemetry` runs when investigating `gpu.fps`, especially if Presented FPS collection becomes part of the measured path.
- Recheck MSI Center SDK initialization and refresh cost after board-provider changes because this machine exercises the MSI board provider by default.
- Recheck NVIDIA provider behavior after NVML, NVAPI, or GPU Engine PDH changes because this machine has only one selected NVIDIA adapter and makes attribution relatively direct.
- Recheck `layout-guide-sheet` after guide-routing changes; this desktop is a useful split case where placement dominates while drawing remains low enough to expose routing work.
