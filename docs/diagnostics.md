# CaseDash Diagnostics

This document owns diagnostics CLI behavior, output contracts, failure policy, and validation recipes.
See also: [docs/specifications.md](specifications.md) for general user-visible runtime behavior, [docs/layout_edit.md](layout_edit.md) for live layout-edit behavior, [docs/layout.md](layout.md) for config language, [docs/layout_guide_sheet.md](layout_guide_sheet.md) for the layout guide sheet feature spec, and [docs/build.md](build.md) for build and test entrypoints.

## Switches

### Output switches

- `/trace[:path]` writes continuous trace output. Without a path it writes every trace prefix to `casedash_trace.txt`.
- `/trace-prefixes:<names>` filters trace output to a comma-separated list of exact trace prefix names such as `profile` or `profile,renderer`. This switch also enables `/trace` to the default trace file when `/trace` is not specified separately.
- `/dump[:path]` writes the machine-parseable snapshot dump.
- `/screenshot[:path]` writes the rendered dashboard PNG.
- `/layout-guide-sheet[:path]` writes a diagnostics PNG that shows a compact selected-layout overview plus representative cards with layout-edit guides and tooltip-style callouts for documented editable targets.
- `/app-icon[:path]` writes the runtime-rendered app icon PNG for the current resolved theme.
- `/save-config[:path]` writes the minimal config overlay export.
- `/save-full-config[:path]` writes the full embedded-template-shaped config export.

### Source and config overrides

- `/reload` forces a config reload through the normal live-dashboard reload path before exporting outputs.
- `/fake[:path]` uses the built-in synthetic telemetry source or reloads the selected snapshot dump file once per second.
- `/layout:<name>` overrides the active named layout for the current process.
- `/theme:<name>` overrides the active named theme for the current process.
- `/default-config` suppresses the executable-side `config.ini` overlay for the current process.
- `/scale:<value>` overrides the runtime render scale for the current process.
- `/app-icon-size:<pixels>` sets the square `/app-icon` export size. Values from `16` through `1024` are valid, and the default is `256`.

### Render modifiers

- `/blank` exports the blank rendering mode.
- `/edit-layout` enables layout-edit guides for the current process and for screenshot exports.
- `/edit-layout:<widget-name>` forces one visible widget of that type into its hover-equivalent layout-edit guide state for screenshot validation.
- `/edit-layout:horizontal-sizes` renders every visible horizontal size ruler and numbering group.
- `/edit-layout:vertical-sizes` renders every visible vertical size ruler and numbering group.
- `/hover:<x>,<y>` applies a layout-edit hover point in dashboard client coordinates during screenshot exports and enables layout-edit hover affordances for that export.

### Control flow

- `/exit` runs the one-shot headless diagnostics path and does not start the interactive dashboard UI.
- `/elevate` relaunches the current command through the Windows `runas` verb, preserves all other arguments, preserves the current working directory for relative diagnostics paths, waits for the elevated child to exit, and returns the child exit code. If the process is already elevated, the switch is ignored and the current process continues.

### Invalid combinations

- `/blank` cannot be combined with `/fake`.
- `/blank` cannot be combined with `/layout-guide-sheet`.
- `/app-icon-size:<pixels>` must be between `16` and `1024`.
- `/trace-prefixes:<names>` accepts only exact supported trace prefix names.
- With `/trace`, diagnostics validation failures append `diagnostics:validation_failed` with the reason and message before exit.

## Output Paths And File Behavior

- Without an explicit path, output switches write in the current working directory.
- Relative explicit paths also resolve from the current working directory.
- Default filenames are `casedash_trace.txt`, `casedash_dump.txt`, `casedash_screenshot.png`, `casedash_layout_guide_sheet.png`, `casedash_app_icon.png`, `casedash_config.ini`, and `casedash_full_config.ini`.
- Trace output appends UTF-8 text without a BOM and uses the `[trace yyyy-mm-dd hh:mm:ss.mmm] <prefix>:` format. Supported prefix-filter names are `amd_adlx`, `asus_armoury_crate`, `board_vendor`, `crash`, `diagnostics`, `fake`, `fps_etw`, `fps_provider`, `fps_service_client`, `gigabyte_siv`, `gpu_vendor`, `intel_level_zero`, `layout_edit_dialog`, `layout_edit_drag`, `layout_edit_hover`, `layout_edit_modal`, `layout_edit_mouse_tracking`, `layout_edit_tooltip`, `layout_edit_ui`, `layout_switch`, `msi_center`, `nvidia_nvml`, `profile`, `renderer`, `telemetry`, `unsupported_board`, `unsupported_gpu`, and `wallpaper`.
- Snapshot dump, screenshot, layout guide sheet, app icon, minimal config overlay, and full config exports overwrite only their requested target file.
- `/fake` without a path uses the built-in synthetic baseline and reads no external file. The built-in baseline uses the themed `fluxsim` FPS application label.
- `/fake:<path>` reads only the selected snapshot dump file.
- The UI diagnostics save dialogs use the same snapshot dump, screenshot, layout guide sheet, and full config formats as the CLI outputs.

## Runtime Mode Behavior

- Without `/exit`, the application starts the normal dashboard UI and keeps requested diagnostics outputs refreshed while the process runs.
- In UI-attached mode, trace logging continues for the process lifetime and requested snapshot dump, screenshot, layout guide sheet, app icon, and config outputs refresh once per second from the latest runtime state.
- With `/exit`, the application loads config, performs the first update, optionally exports the requested outputs once, and exits without entering the normal GUI lifetime.
- With `/elevate`, trace, snapshot dump, screenshot, layout guide sheet, app icon, config, and layout switches are handled by the elevated child process after relaunch; the unelevated parent does not open diagnostics outputs.
- `/default-config`, `/layout:<name>`, `/theme:<name>`, and `/scale:<value>` stay active for the full process lifetime, including `/reload` runs inside that process.
- `/reload /exit` performs the normal first startup and update path, reloads through the live-dashboard reload logic, then exports from the reloaded state.
- `/fake` without `/exit` advances the built-in synthetic source on the telemetry-owned 250 ms refresh path so the dashboard exercises live fake values and the same retained-throughput smoothing as real telemetry.
- `/fake /exit` keeps the built-in synthetic source on its static baseline so deterministic diagnostics exports stay stable.
- `/fake:<path>` reloads the selected fake file from the telemetry-owned refresh path while the process runs so manual edits affect a later telemetry-published snapshot.
- Screenshot exports use the same Direct2D and DirectWrite scene as the live dashboard draw path, so exported images match live styling, scale, and blank-mode behavior. Screenshot PNG exports are encoded as opaque 24-bit images because the rendered diagnostics surfaces include their own background.
- App-icon exports use the same programmatic icon renderer as the live window, tray, and dialog icons, write compressed PNG files from the generated icon bitmap, use the current resolved theme colors including the dashboard card fill composited over the app background, and do not depend on dashboard render scale.
- App-icon exports create the target parent directory when it is missing.
- Unhandled native process crashes write a best-effort minidump and text report named `casedash_crash_<timestamp>_<pid>.dmp` and `casedash_crash_<timestamp>_<pid>.txt`. Crash files are created in the launch working directory when it is writable, otherwise in the process temp directory.
- Layout guide sheet exports follow the feature contract in [docs/layout_guide_sheet.md](layout_guide_sheet.md) and refresh once per second in UI-attached diagnostics mode.
- When `/trace` and `/screenshot` are both enabled, each screenshot export writes `diagnostics:active_region` trace lines from the `LayoutEditActiveRegions` snapshot for mouse-reactive dashboard regions that are present in the exported frame, including card and widget hover regions, layout guides, container-child reorder targets, gap handles, widget guides, text anchors, and color targets. Each line includes the client-coordinate box, visual type, config or layout path, and a short detail string; a `diagnostics:active_regions` summary records the exported count.
- Headless trace output writes one `diagnostics:resolved_color` line per resolved `[colors]` and `[layout_guide_sheet]` color after startup config resolution and after a successful `/reload`. Each line includes the config section, color name, resolved `#RRGGBBAA` value, and source expression when the color came from a config expression.
- When `/trace` is enabled with the `profile` prefix included, the app collects high-precision timing samples for the real runtime operations that mirror benchmark phases and writes `profile:timing` summaries about every 10 seconds, plus a final partial flush when the trace stream closes. Each summary line reports one operation with `op`, `samples`, `total_ms`, `avg_ms`, and `interval_ms`; current operations include `telemetry_update`, `hover_hit_test`, `snap`, `apply`, `paint_total`, `paint_draw`, `presentation_frame_build`, `presentation_resolve_metrics`, `snapshot_layer_bitmap`, `snapshot_layer_content`, `dynamic_edit_collisions`, `overlay_layer_bitmap`, `overlay_layer_content`, `presentation_frame_publish`, and `animation_frame`. The `animation_frame` operation measures animation sampling and composition work only; it excludes the live DXGI vsync wait. Use `/trace-prefixes:profile` for timing-only trace output without verbose provider logging.
- When `/trace` and `/layout-guide-sheet` are both enabled, each layout guide sheet export writes a `diagnostics:layout_guide_sheet` start marker, one `diagnostics:layout_guide_sheet detail` line per collected render detail such as canvas dimensions, leader scores, selected cards, placed callout count, and remaining leader intersections, one detail line per remaining intersection with its card, kind, sides, and callout keys, one `diagnostics:layout_guide_sheet stats` line with selected-card and callout counts plus active-region, planning, measurement, placement, and draw timings, and then an end marker.
- When `/trace` and `/app-icon` are both enabled, each successful icon export writes `diagnostics:app_icon_saved`; failures write `diagnostics:app_icon_save_failed` with the target path, size, and error detail when available.
- When `/hover:<x>,<y>` is active during a traced screenshot export, the trace writes one `diagnostics:hover` line with the hover point, resolved target kind, and tooltip text that the live layout-edit UI would show. If no hover target resolves, the line reports `target="none"`.
- Live layout-edit tooltips use a separate Win32 tooltip window and therefore do not appear in diagnostics screenshots.

## Failure And Trace Policy

- The diagnostics trace covers startup, reload, output export, renderer layout data, runtime timing summaries, telemetry collection, hardware-provider activity, presented-FPS service client probes, presented-FPS ETW probes, unsupported-provider markers, and focused interactive layout-edit UI markers for layout switches, modal-menu scope, dialog-tree refresh, tooltip show or hide, and capture-state transitions when `/trace` is enabled. Provider-specific trace markers are listed in [docs/hardware.md](hardware.md).
- Presented-FPS tracing reports `fps_service_client:*` service-client startup and sample failures when the `CashDashService` LocalSystem service is unavailable or stops responding, and reports `fps_etw:*` startup, provider-enable, sampled-present, and shutdown markers when local ETW collection is used. Presented-FPS sample diagnostics include cumulative source-event counts, the cleaned selected process name or `!admin` when Windows denies process-name access, the selected process window count, the raw rolling FPS value, the smoothed displayed FPS value, and GPU Engine 3D selection details such as `top_gpu3d_process`, `top_gpu3d_pid`, `top_gpu3d`, and `selected_gpu3d`. If Windows denies ETW access or process-name access, diagnostics mark `gpu.fps` with the `permission_required` issue and record the failure in the active GPU provider diagnostics.
- Diagnostics failures that occur while opening or writing outputs are written to trace before any error dialog is shown.
- Unhandled native process crashes append a `crash:unhandled_exception` trace line when `/trace` is active and the trace file can be reopened. The line includes the exception code, exception address, crash report path, and minidump path.
- When `/trace` is enabled, diagnostics failures prefer trace logging plus a failure exit code over blocking modal behavior.
- Required fake-file load failures follow that same rule so `/fake:<path> /exit` returns promptly under trace.
- Telemetry runtime creation reports initialization failure detail to its caller; diagnostics and dashboard callers own trace or dialog reporting.
- Layout-edit drag profiling writes one start marker and one end marker per drag with summarized timing instead of high-volume per-frame renderer trace spam.
- The modeless layout-edit dialog writes focused trace markers for tree rebuild, tree viewport restoration, tree selection, layout/theme preview, field preview, and color-picker flows when trace is enabled.

## Snapshot Dump Contract

- The snapshot dump contains only the runtime snapshot model that the dashboard renders and that `/fake` reloads.
- The snapshot dump includes retained histories, configured drive rows, and local date and time down to milliseconds.
- Retained histories store raw sampled values in native runtime units.
- The current snapshot dump format version is `casedash_snapshot_v12`.
- Snapshot dump GPU FPS fields include the optional cleaned presenting application name as `gpu.fps.app_name`.
- Snapshot dump scalar-unit fields use only the canonical snapshot dump tokens: the empty string plus `C`, `GHz`, `MHz`, `FPS`, and `RPM`.
- Snapshot dump scalar issue fields use `none` or `permission_required`.
- Provider-specific diagnostics and trace-only debug details do not appear in the snapshot dump schema.

## Single-Instance Interaction

- `/exit` runs outside the normal single-instance dashboard replacement path.
- Diagnostics switches without `/exit` stay on the normal UI startup path and therefore follow the standard single-instance behavior.
- `/fake` by itself also stays on the normal UI path.

## Validation Recipes

- Build first through `build.cmd`.
- Include `/trace` during diagnostics validation and inspect trace output even when the main change affects snapshot dump or screenshot behavior.
- When validation is meant to exercise the built-in config, add `/default-config`.
- Put explicit diagnostics paths under `build\` so repository files stay clean.
- Prefer the smallest traced `/exit` command that exercises the changed behavior.
- Add only the modifiers that matter to the change: `/reload`, `/blank`, `/layout:<name>`, `/theme:<name>`, `/edit-layout`, `/edit-layout:<widget-name>`, `/hover:<x>,<y>`, `/layout-guide-sheet`, `/app-icon`, `/save-config`, or `/save-full-config`.

Recommended coverage:

- Trace plus snapshot dump validates snapshot content and provider state.
- Trace plus screenshot validates rendered output and active-region trace data.
- Trace plus config export validates minimal or full config output.
- Trace plus layout guide sheet validates layout guide sheet planning, placement, and trace details.
- Trace plus app icon validates themed icon rendering and output paths.
- UI-attached diagnostics validate once-per-second refresh behavior when live refresh, hover-only behavior, drag-only behavior, or interactive fake-runtime reloads changed.
- Headless `/fake /exit` and interactive `/fake` both matter when fake-runtime startup or reload behavior changed.

Layout-edit validation:

- Inspect `diagnostics:active_region` lines to verify mouse-reactive region geometry and layout paths.
- Add `/hover:<x>,<y>` to a traced screenshot command and inspect `diagnostics:hover` when tooltip targeting changed.
- Use `/edit-layout:<widget-name>` for each widget class whose edit chrome changed.
- Use `/edit-layout:horizontal-sizes` or `/edit-layout:vertical-sizes` when ruler grouping changed.
- Validate the presence of the intended guide family or ruler grouping rather than re-documenting widget-specific edit semantics here; user-visible layout-edit behavior is defined in [docs/layout_edit.md](layout_edit.md).
