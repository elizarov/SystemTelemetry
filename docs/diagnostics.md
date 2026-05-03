# CaseDash Diagnostics

This document owns diagnostics CLI behavior, output contracts, failure policy, and validation recipes.
See also: [docs/specifications.md](specifications.md) for user-visible runtime behavior, [docs/layout.md](layout.md) for config language, [docs/layout_guide_sheet.md](layout_guide_sheet.md) for the layout guide sheet feature spec, and [docs/build.md](build.md) for build and test entrypoints.

## Switches

### Output switches

- `/trace[:path]` writes continuous trace output.
- `/dump[:path]` writes the machine-parseable snapshot dump.
- `/screenshot[:path]` writes the rendered dashboard PNG.
- `/layout-guide-sheet[:path]` writes a diagnostics PNG that shows a compact selected-layout overview plus representative cards with layout-edit guides and tooltip-style callouts for documented editable targets.
- `/app-icon[:path]` writes the runtime-rendered app icon PNG for the current resolved theme.
- `/save-config[:path]` writes the minimal config overlay export.
- `/save-full-config[:path]` writes the full embedded-template-shaped config export.

### Source and config overrides

- `/reload` forces a config reload through the normal live-dashboard reload path before exporting outputs.
- `/fake[:path]` uses the built-in synthetic telemetry source or reloads the selected fake dump file once per second.
- `/layout:<name>` overrides the active named layout for the current process.
- `/theme:<name>` overrides the active named theme for the current process.
- `/default-config` suppresses the executable-side `config.ini` overlay for the current process.
- `/scale:<value>` overrides the runtime render scale for the current process.
- `/app-icon-size:<pixels>` sets the square `/app-icon` export size. Values from `16` through `1024` are valid, and the default is `256`.

### Render modifiers

- `/blank` exports the blank rendering mode.
- `/edit-layout` enables layout-edit guides for the current process and for screenshot exports.
- `/edit-layout:<widget-name>` forces one visible widget of that type into its hover-equivalent layout-edit guide state for screenshot validation.
- `/edit-layout:horizonatal-sizes` renders every visible horizontal size ruler and numbering group.
- `/edit-layout:vertical-sizes` renders every visible vertical size ruler and numbering group.
- `/hover:<x>,<y>` applies a layout-edit hover point in dashboard client coordinates during screenshot exports and enables layout-edit hover affordances for that export.

### Control flow

- `/exit` runs the one-shot headless diagnostics path and does not start the interactive dashboard UI.
- `/elevate` relaunches the current command through the Windows `runas` verb, preserves all other arguments, preserves the current working directory for relative diagnostics paths, waits for the elevated child to exit, and returns the child exit code. If the process is already elevated, the switch is ignored and the current process continues.

### Invalid combinations

- `/blank` cannot be combined with `/fake`.
- `/blank` cannot be combined with `/layout-guide-sheet`.
- `/app-icon-size:<pixels>` must be between `16` and `1024`.
- With `/trace`, diagnostics validation failures append `diagnostics:validation_failed` with the reason and message before exit.

## Output Paths And File Behavior

- Without an explicit path, output switches write in the current working directory.
- Relative explicit paths also resolve from the current working directory.
- Default filenames are `casedash_trace.txt`, `casedash_dump.txt`, `casedash_screenshot.png`, `casedash_layout_guide_sheet.png`, `casedash_app_icon.png`, `casedash_config.ini`, and `casedash_full_config.ini`.
- Trace output appends UTF-8 text without a BOM and uses the `[trace yyyy-mm-dd hh:mm:ss.mmm]` prefix format.
- Dump, screenshot, layout-guide-sheet, app-icon, minimal-config, and full-config exports overwrite only their requested target file.
- `/fake` without a path uses the built-in synthetic baseline and reads no external file.
- `/fake:<path>` reads only the selected dump file.
- The UI diagnostics save dialogs use the same dump, screenshot, layout-guide-sheet, and full-config formats as the CLI outputs.

## Runtime Mode Behavior

- Without `/exit`, the application starts the normal dashboard UI and keeps requested diagnostics outputs refreshed while the process runs.
- In UI-attached mode, trace logging continues for the process lifetime and requested dump, screenshot, layout-guide-sheet, app-icon, and config outputs refresh once per second from the latest runtime state.
- With `/exit`, the application loads config, performs the first update, optionally exports the requested outputs once, and exits without entering the normal GUI lifetime.
- With `/elevate`, trace, dump, screenshot, layout-guide-sheet, app-icon, config, and layout switches are handled by the elevated child process after relaunch; the unelevated parent does not open diagnostics outputs.
- `/default-config`, `/layout:<name>`, `/theme:<name>`, and `/scale:<value>` stay active for the full process lifetime, including `/reload` runs inside that process.
- `/reload /exit` performs the normal first startup and update path, reloads through the live-dashboard reload logic, then exports from the reloaded state.
- `/fake:<path>` reloads the selected fake file from the telemetry-owned refresh path while the process runs so manual edits affect a later telemetry-published snapshot.
- Screenshot exports use the same Direct2D and DirectWrite scene as the live dashboard draw path, so exported images match live styling, scale, and blank-mode behavior.
- App-icon exports use the same programmatic icon renderer as the live window, tray, and dialog icons, use WIC PNG encoding, and use the current resolved theme colors without depending on dashboard render scale.
- Layout guide sheet exports select the smallest practical card subset that covers the visible widget types in the selected layout, render those cards as separate specimens with forced layout-edit affordances, and use the same active-region geometry and shared layout-edit tooltip text as live editing; callouts document active areas inside rendered representative cards, render each documented target in its hover-equivalent state, group equivalent metric-definition rows into one representative hovered row, omit callouts for non-rendered cards and outside-card layout guides, use local left and right side stacks with one promoted top and one promoted bottom callout per annotated block when available, grow the canvas to fit tooltip-style callouts around the card column, and refresh once per second in UI-attached diagnostics mode.
- When `/trace` and `/screenshot` are both enabled, each screenshot export writes `diagnostics:active_region` trace lines from the `LayoutEditActiveRegions` snapshot for mouse-reactive dashboard regions that are present in the exported frame, including card and widget hover regions, layout guides, container-child reorder targets, gap handles, widget guides, text anchors, and color targets. Each line includes the client-coordinate box, visual type, config or layout path, and a short detail string; a `diagnostics:active_regions` summary records the exported count.
- Headless trace output writes one `diagnostics:resolved_color` line per resolved `[colors]` and `[layout_guide_sheet]` color after startup config resolution and after a successful `/reload`. Each line includes the config section, color name, resolved `#RRGGBBAA` value, and source expression when the color came from a config expression.
- When `/trace` and `/layout-guide-sheet` are both enabled, each guide-sheet export writes a `diagnostics:layout_guide_sheet` start marker, one `diagnostics:layout_guide_sheet detail` line per collected render detail such as canvas dimensions, leader scores, selected cards, placed callout count, and remaining leader intersections, one detail line per remaining intersection with its card, kind, sides, and callout keys, one `diagnostics:layout_guide_sheet stats` line with selected-card and callout counts plus active-region, planning, measurement, placement, and draw timings, and then an end marker.
- When `/trace` and `/app-icon` are both enabled, each successful icon export writes `diagnostics:app_icon_saved`; failures write `diagnostics:app_icon_save_failed` with the target path, size, and error detail when available.
- When `/hover:<x>,<y>` is active during a traced screenshot export, the trace writes one `diagnostics:hover` line with the hover point, resolved target kind, and tooltip text that the live layout-edit UI would show. If no hover target resolves, the line reports `target="none"`.
- Live layout-edit tooltips use a separate Win32 tooltip window and therefore do not appear in diagnostics screenshots.

## Failure And Trace Policy

- The diagnostics trace covers startup, reload, output export, renderer layout data, telemetry collection, vendor-provider activity including GPU adapter vendor selection, AMD ADLX, NVIDIA NVML, NVIDIA presented-FPS service client probes, NVIDIA presented-FPS ETW probes, `unsupported_gpu`, `msi_center`, `gigabyte_siv`, and `unsupported_board` provider markers, and focused interactive layout-edit UI markers for layout switches, modal-menu scope, dialog-tree refresh, tooltip show or hide, and capture-state transitions when `/trace` is enabled.
- NVIDIA FPS tracing reports `fps_service_client:*` service-client startup and sample failures when the `CashDashService` LocalSystem service is unavailable or stops responding, and reports `fps_etw:*` startup, provider-enable, sampled-present, and shutdown markers when local ETW collection is used. NVIDIA FPS sample diagnostics include cumulative source-event counts and the selected process window count. If Windows denies ETW access, diagnostics mark `gpu.fps` with the `permission_required` issue and record the Win32 failure in the NVIDIA provider diagnostics.
- Diagnostics failures that occur while opening or writing outputs are written to trace before any error dialog is shown.
- When `/trace` is enabled, diagnostics failures prefer trace logging plus a failure exit code over blocking modal behavior.
- Required fake-file load failures follow that same rule so `/fake:<path> /exit` returns promptly under trace.
- Telemetry runtime creation reports initialization failure detail to its caller; diagnostics and dashboard callers own trace or dialog reporting.
- Layout-edit drag profiling writes one start marker and one end marker per drag with summarized timing instead of high-volume per-frame renderer trace spam.
- The modeless layout-edit dialog writes focused trace markers for tree rebuild, tree viewport restoration, tree selection, layout/theme preview, field preview, and color-picker flows when trace is enabled.

## Dump Contract

- The dump contains only the runtime snapshot model that the dashboard renders and that `/fake` reloads.
- The dump includes retained histories, configured drive rows, and local date and time down to milliseconds.
- Retained histories store raw sampled values in native runtime units.
- The current dump format version is `casedash_snapshot_v10`.
- Dump scalar-unit fields use only the canonical dump tokens: the empty string plus `C`, `GHz`, `MHz`, `FPS`, and `RPM`.
- Dump scalar issue fields use `none` or `permission_required`.
- Provider-specific diagnostics and trace-only debug details do not appear in the dump schema.

## Single-Instance Interaction

- `/exit` runs outside the normal single-instance dashboard replacement path.
- Diagnostics switches without `/exit` stay on the normal UI startup path and therefore follow the standard single-instance behavior.
- `/fake` by itself also stays on the normal UI path.

## Validation Recipes

- Build first through `build.cmd`.
- Include `/trace` during diagnostics validation and inspect trace output even when the main change affects dump or screenshot behavior.
- For layout-edit screenshot diagnostics, inspect `diagnostics:active_region` lines to verify mouse-reactive region geometry and layout paths.
- For headless hover validation, add `/hover:<x>,<y>` to the traced screenshot command and inspect the `diagnostics:hover` tooltip text.
- When validation is meant to exercise the built-in config, add `/default-config`.
- Put explicit diagnostics paths under `build\` so repository files stay clean.

Recommended checks:

- UI-attached `/trace`, `/dump`, `/screenshot`, and `/trace /dump /screenshot`
- Headless `/trace /default-config /dump /screenshot /exit`
- Headless `/trace /default-config /reload /screenshot /exit`
- Headless `/trace /default-config /layout:<name> /save-config /save-full-config /exit`
- Headless `/trace /blank /screenshot /exit`
- One headless run with explicit output filenames for trace, dump, and screenshot
- One headless `/trace /default-config /layout:<name> /screenshot /exit`
- One headless `/trace /default-config /theme:<name> /screenshot /exit`
- One headless `/trace /default-config /edit-layout /screenshot /exit`
- One headless `/trace /default-config /layout-guide-sheet /exit`
- One headless `/trace /default-config /app-icon /exit`
- One headless `/trace /default-config /theme:<name> /app-icon:<path> /app-icon-size:256 /exit`
- One headless `/trace /default-config /edit-layout /hover:<x>,<y> /screenshot /exit`
- One headless `/trace /default-config /edit-layout:<widget-name> /screenshot /exit` for each widget class whose edit chrome changed
- One headless `/trace /default-config /edit-layout:horizonatal-sizes /screenshot /exit`
- One headless `/trace /default-config /edit-layout:vertical-sizes /screenshot /exit`
- UI `/edit-layout` verification when hover-only or drag-only artifacts changed
- Both interactive `/fake` and headless `/fake /exit` when fake-runtime behavior changed

When layout-edit screenshots are involved, validate the presence of the intended guide family or ruler grouping rather than re-documenting widget-specific edit semantics here; the user-visible layout-edit behavior itself is defined in [docs/specifications.md](specifications.md).
