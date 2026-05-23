# Gigabyte X570 I + AMD RX 6800 Performance

This file records current benchmark ranges, bottlenecks, and next research directions for this test machine. It intentionally does not keep a journal of past experiments.

## Machine Details

- Board: Gigabyte Technology Co., Ltd. X570 I AORUS PRO WIFI.
- GPUs: Advanced Micro Devices, Inc. AMD Radeon RX 6800, dedicated memory `15.96 GiB` visible to DXGI, nominal `16 GB`.
- System memory: `63.94 GiB` visible, installed as `2 x 32 GB` Kingston `KF3600C18D4/32GX`.
- Hardware providers used by current traces: Gigabyte SIV for board telemetry, AMD ADLX for GPU telemetry, and Presented FPS ETW for active-presenter FPS.

## Current Benchmark Ranges

Ranges use direct `build\CaseDashBenchmarks.exe` runs unless a note says otherwise. Update these ranges after repeated direct runs on a fresh benchmark build.

| Benchmark | Current known range | Important split |
| --- | --- | --- |
| `animation 240 2` | `animation_loop` and `animation_frame` about `0.43-0.53 ms` | Current direct rerun reports `snapshot_animations=28`, `overlay_animations=0`, and `active_chunk_frames=120`. |
| `snapshot-handoff 20 2` | `snapshot_loop` about `7.9-8.2 ms` | `presentation_frame_build` carries nearly all cost; `presentation_frame_publish` stays near `0.00 ms`. |
| `edit-layout 240 2` | `drag_loop` about `2.36-2.60 ms` | `snap` about `0.07-0.09 ms`, `apply` about `0.05-0.06 ms`, `paint_draw` about `2.22-2.43 ms`. |
| `layout-guide-sheet 20 2` | `sheet_loop` about `90-126 ms` | `sheet_place` is the dominant split, recently about `43-76 ms`; `sheet_draw` is usually about `30-36 ms`. |
| `layout-switch 240 2` | `switch_loop` about `3.52-4.08 ms` | `switch_apply` about `0.80-0.97 ms`, `dialog_refresh` about `0.16-0.24 ms`, `switch_paint` about `2.57-2.84 ms`. |
| `mouse-hover 240 2` | `hover_loop` about `1.06-1.32 ms` on the retained-overlay path | `hover_hit_test` is no longer dominant; repaint is mostly overlay plus composition. |
| `theme-change 240 2` | `theme_loop` about `3.86-4.71 ms` | `dashboard_config` about `0.83-1.13 ms`, `theme_preview` about `0.40-0.78 ms` in current cached runs, `theme_paint` about `2.30-2.55 ms`. |
| `update-telemetry 240 2` | `update_loop` about `4.76-4.89 ms` | `telemetry_update` about `2.57-2.80 ms`, `paint_draw` about `2.08-2.20 ms`. |

## Current Bottlenecks

- `animation` is dominated by Direct2D/DXGI composition, retained dirty-region restore, render-thread request handoff, timeline lookup, and widget animation drawing. Transition sampling is not the main limiter.
- `snapshot-handoff` is frame-build bound in `DashboardRenderer::BuildPresentationFrame` and live layer bitmap drawing; publish cost is negligible.
- `edit-layout`, `layout-switch`, `theme-change`, and the retained-overlay `mouse-hover` path are mainly Direct2D, DirectWrite, text-shaping, layer drawing, and driver-stack work once snap, apply, hover hit testing, and edit-tree refresh stay in range.
- `layout-guide-sheet` remains placement-score bound. Leader intersection scoring and side repair dominate before offscreen drawing.
- `update-telemetry` is mildly collector-bound on this hardware. The remaining provider cost is concentrated in real Gigabyte SIV board sampling, AMD ADLX GPU sampling, and shared GPU/FPS update work, while repaint remains the other large half of the loop.
- `PDH.DLL` is not currently the dominant telemetry hotspot after raw GPU Engine sampling and ADLX load/VRAM preference.

## Further Research Directions

- Use WPA or call-tree HTML when investigating draw cost; the flat text export often hides useful app-side attribution behind Direct2D, DirectWrite, the AMD display driver, or WIC.
- Explore reducing DirectWrite/text-shaping work in the live Direct2D scene without reintroducing a second renderer path.
- Explore throughput chart and gauge geometry reductions that preserve the current pixels and avoid hybrid GDI/Direct2D interop.
- For `layout-guide-sheet`, focus on local leader-routing repairs and cheaper intersection scoring rather than global side-split or stack-order search.
