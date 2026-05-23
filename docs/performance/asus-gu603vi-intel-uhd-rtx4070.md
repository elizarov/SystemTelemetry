# ASUS GU603VI + Intel UHD + NVIDIA RTX 4070 Performance

This file records current benchmark ranges, bottlenecks, and next research directions for this test machine. It intentionally does not keep a journal of past experiments.

## Machine Details

- Board: ASUSTeK COMPUTER INC. GU603VI, version `1.0`, system model `ROG Zephyrus G16 GU603VI_GU603VI`.
- CPU: 13th Gen Intel Core i9-13900H, `14` cores and `20` logical processors.
- GPUs: Intel(R) UHD Graphics, DXGI dedicated memory about `0.12 GiB`; NVIDIA GeForce RTX 4070 Laptop GPU, DXGI dedicated memory about `7.76 GiB`, `8188 MiB` reported by `nvidia-smi`, nominal `8 GB`.
- System memory: `15.63 GiB` visible, installed as `1 x 16 GB` Micron Technology `ATF2G64AZ-3G2F1` at `3200 MT/s`.
- Hardware providers used by current traces: ASUS Armoury Crate ATKACPI for board CPU temperature plus CPU and GPU fan RPM, Intel Level Zero for the default selected GPU, NVIDIA NVML/NVAPI when the NVIDIA adapter is selected, and Presented FPS ETW when `gpu.fps` is requested and elevation or service access is available.

## Current Benchmark Ranges

Ranges use direct `build\CaseDashBenchmarks.exe` runs from a fresh `build.cmd /benchmarks` build unless a note says otherwise. The first ranges are from three direct passes on this machine.

| Benchmark | Current known range | Important split |
| --- | --- | --- |
| `animation 240 2` | `animation_loop` and `animation_frame` about `0.40-0.77 ms` | Current direct reruns report `snapshot_animations=28`, `overlay_animations=0`, and `active_chunk_frames=120`; later passes were `0.40-0.48 ms`. |
| `snapshot-handoff 20 2` | `snapshot_loop` about `3.61-4.41 ms` | `presentation_frame_build` carries nearly all cost at about `3.60-4.41 ms`; `presentation_frame_publish` stays near `0.00-0.01 ms`. |
| `edit-layout 240 2` | `drag_loop` about `2.45-4.20 ms` | `snap` about `0.10-0.20 ms`, `apply` about `0.07-0.13 ms`, `paint_draw` about `2.25-3.82 ms`. |
| `layout-guide-sheet 20 2` | `sheet_loop` about `200-203 ms` | `sheet_draw` is about `84.9-88.1 ms`, `sheet_place` about `67.2-68.9 ms`, `sheet_measure` about `20.2-22.0 ms`, and `active_regions` about `24.0-24.3 ms`. |
| `layout-switch 240 2` | `switch_loop` about `5.31-5.98 ms` | `switch_apply` about `0.90-0.97 ms`, `dialog_refresh` about `0.23-0.24 ms`, `switch_paint` about `4.14-4.72 ms`. |
| `mouse-hover 240 2` | `hover_loop` about `0.93-1.06 ms` | `hover_hit_test` about `0.16-0.18 ms`; repaint is about `0.76-0.90 ms`. |
| `theme-change 240 2` | `theme_loop` about `3.68-5.49 ms` | `dashboard_config` about `0.71-1.00 ms`, `theme_preview` about `0.38-0.63 ms`, `theme_paint` about `2.30-3.43 ms`. |
| `telemetry-init 2 2`, default layout with synchronous provider samples | `iteration_loop` about `151-235 ms` | `collector_initialize` about `143-226 ms`; default adapter selection uses Intel Level Zero, and the default layout requests ASUS board-backed temperature and fan metrics. |
| `update-telemetry 240 2`, default layout with synchronous provider samples | `update_loop` about `4.13-4.64 ms` | `telemetry_update` about `1.92-2.17 ms`, `paint_draw` about `2.21-2.47 ms`; initialization is not included in the loop line. |

## Current Bottlenecks

- `layout-guide-sheet` is split between offscreen drawing and placement work on this machine. `sheet_draw` is the largest measured split in the first direct passes, while leader placement and measurement remain substantial.
- `edit-layout`, `layout-switch`, `theme-change`, and `mouse-hover` are mainly repaint-bound after snap, apply, hover hit testing, config copy, and edit-tree refresh stay small.
- `animation` and `snapshot-handoff` are Direct2D/DXGI layer-pipeline work. The snapshot handoff loop is frame-build bound and publish cost stays negligible.
- Telemetry refresh is balanced between real provider collection and repaint. ASUS ATKACPI board sampling, Intel Level Zero GPU sampling, and shared GPU/FPS update work stay comfortably under the paint cost in the first direct runs.
- Local Presented FPS ETW is unavailable in the normal unelevated diagnostic trace on this machine with `Access is denied`; use elevation or `CashDashService` when validating `gpu.fps` behavior rather than treating that diagnostic state as a benchmark regression.

## Further Research Directions

- Refresh these ranges after any ASUS board-provider or Intel Level Zero provider changes because this machine exercises both by default.
- Run elevated or service-backed `update-telemetry` comparisons when investigating `gpu.fps`, especially if Presented FPS collection becomes part of a measured telemetry path.
- Compare an explicit NVIDIA-adapter config overlay against the default Intel-adapter baseline before drawing conclusions about RTX 4070 laptop provider cost.
- Recheck `layout-guide-sheet` after rendering or guide-routing changes; this machine currently makes offscreen drawing at least as visible as placement scoring.
