# System Telemetry diagnostics

This document is the single maintained source of truth for diagnostics command behavior, validation commands, and diagnostics-output inspection guidance.

## Command-line switches

- `/trace[:path]` enables continuous trace logging to `telemetry_trace.txt` in the current working directory, or to the optional target path.
- `/dump[:path]` writes a machine-parseable snapshot dump to `telemetry_dump.txt` in the current working directory, or to the optional target path.
- `/screenshot[:path]` writes a rendered dashboard PNG to `telemetry_screenshot.png` in the current working directory, or to the optional target path.
- `/save-config[:path]` writes a minimal INI overlay to `telemetry_config.ini` in the current working directory, or to the optional target path.
- `/save-full-config[:path]` writes a full embedded-template-shaped INI export to `telemetry_full_config.ini` in the current working directory, or to the optional target path.
- `/reload` forces a config reload through the normal live-dashboard reload path before headless diagnostics outputs are exported.
- `/exit` runs diagnostics as a one-shot headless export path instead of starting the dashboard UI.
- `/fake[:path]` replaces live telemetry collection with periodic reads from `telemetry_fake.txt` in the current working directory, or from the optional fake dump path.
- `/layout:<name>` overrides `display.layout` for the current process so diagnostics can validate a named layout without editing `config.ini`.
- `/default-config` suppresses loading the executable-side `config.ini` overlay and uses only the embedded `resources/config.ini` defaults for the current process.
- `/blank` switches screenshot rendering into a blank background mode that keeps static dashboard chrome and static text such as CPU and GPU names while omitting dynamic metric text, time, date, plots, leaders, peak ghosts, gauge fill, and drive activity or usage fill.
- `/scale:<value>` multiplies headless `/screenshot /exit` render size, including all measured layout geometry, and accepts fractional values such as `1.5`.
- `/edit-layout:<widget-name>` keeps layout-edit guides enabled and, for screenshots, forces the first visible widget of that type into the same outline and widget-guide state that live UI hover would show.
- `/edit-layout:horizonatal-sizes` turns on layout-edit guides plus every visible horizontal size ruler and its exact-match numbering for diagnostics screenshots without requiring an active drag.
- `/edit-layout:vertical-sizes` turns on layout-edit guides plus every visible vertical size ruler and its exact-match numbering for diagnostics screenshots without requiring an active drag.
- `/blank` cannot be combined with `/fake`.

## Output files

- Without an explicit path, `/trace`, `/dump`, `/screenshot`, `/save-config`, and `/save-full-config` write in the current working directory using their default filenames.
- With an explicit path, each switch writes the same content format to that requested path instead of the default file.
- Without an explicit path, `/fake` reads `telemetry_fake.txt` from the current working directory.
- With an explicit path, `/fake` reads that requested dump file instead of the default fake file.
- Relative diagnostics paths resolve from the current working directory.
- The UI `Diagnostics` submenu uses a standard Save dialog and defaults `Save full config to...`, `Save dump to...`, and `Save screenshot to...` to the current working directory with the same default file names used by `/save-full-config`, `/dump`, and `/screenshot`.
- Trace output appends plain UTF-8 text without a BOM and uses the prefix format `[trace yyyy-mm-dd hh:mm:ss.mmm]`.
- Dump output overwrites with a stable text format that can be copied directly into the default fake file or a `/fake` target file.
- Screenshot output overwrites with only the rendered dashboard PNG.
- `/save-config` overwrites with only the changed live values relative to the target file's current loaded config state.
- `/save-full-config` overwrites with the full embedded-template line structure updated to the live config values.

## Runtime behavior

- Without `/exit`, the application starts the normal dashboard UI and keeps producing any requested diagnostics outputs while it runs.
- In UI-attached mode, trace logging continues for the process lifetime, while dump and screenshot outputs refresh once per second from the latest snapshot.
- With `/exit`, the application initializes telemetry from the normal runtime `config.ini`, performs the first update, optionally writes the requested outputs once, and exits without starting the GUI.
- With `/default-config`, the application skips the executable-side `config.ini` overlay and keeps only the embedded default config for startup and `/reload` diagnostics runs.
- With `/layout:<name>`, the application applies that named layout after loading `config.ini` and keeps that override active for the rest of the process, including `/reload` diagnostics runs.
- With `/reload /exit`, the application completes the normal first startup and update path, reloads config through the same live-dashboard logic, and exports outputs from the reloaded state.
- With `/fake`, the application skips live telemetry providers, loads the selected fake dump file immediately, and reloads it once per second while the process runs.
- With `/blank`, the application keeps the normal layout, panel chrome, card headers, CPU and GPU names, drive labels, and empty chart or bar tracks while suppressing dynamic metric rendering.
- `/edit-layout` diagnostics outputs always include the resolved container guides, while widget-local guides appear only when the live UI is hovering a supported widget or when `/edit-layout:<widget-name>` forces one supported widget type.
- `/edit-layout:<widget-name>` fails the screenshot export when the requested widget type does not exist in the active layout.

## Failures and trace coverage

- The diagnostics trace covers all diagnostics collection paths, not only vendor GPU integration.
- Trace lines for telemetry or vendor API calls should include returned status or result codes plus key sampled values that help explain missing metrics or failures.
- Diagnostics output failures such as trace, dump, screenshot, or config-export file-open or write failures must be written to the trace before any error dialog is shown.
- When `/trace` is enabled, diagnostics failures prefer trace logging over modal UI and complete with a failure exit code.
- Required `/fake` load failures follow that same rule so `/fake /exit` returns promptly with trace output.
- When the renderer initializes under `/trace`, the trace also includes measured font heights, computed layout heights, and resolved widget and card rectangles.

## Dump content

- The dump includes only the current runtime snapshot model that `/fake` loads and the dashboard renders, including retained histories, configured drive rows with per-drive read and write MB/s, and the full local date and time down to milliseconds.
- The dump schema reflects the current runtime model directly and removes obsolete compatibility keys instead of keeping null placeholders.
- Provider diagnostics and provider-specific debug details stay in trace output instead of being duplicated into the dump schema.

## Single-instance behavior

- `/exit` runs outside the normal single-instance behavior as an independent one-shot command.
- Diagnostics switches without `/exit` stay on the normal UI startup path and follow the standard single-instance replacement behavior.
- `/fake` by itself stays on the normal UI startup path and follows the standard single-instance replacement behavior.

## Validation

- Always rebuild through `build.cmd` before validating diagnostics changes.
- Always include `/trace` in diagnostics validation and inspect trace output, even when the primary change affects dump or screenshot behavior.
- When headless validation is meant to exercise the built-in config, always add `/default-config` so executable-side `config.ini` does not mask the embedded defaults.
- When validating default diagnostics paths, launch the executable from the intended working directory and confirm the default files land there.
- When validation commands specify diagnostics paths explicitly, point them somewhere under `build\` so trace, dump, screenshot, and fake files do not pollute the repository root.
- Verify UI-attached `/trace`, `/dump`, `/screenshot`, and `/trace /dump /screenshot`.
- Verify headless `/trace /default-config /dump /screenshot /exit` and `/trace /default-config /reload /screenshot /exit` when validating the built-in config, and confirm the process exits after the requested export path.
- Verify headless `/trace /default-config /layout:<name> /save-config /save-full-config /exit`, confirm the minimal config contains only the changed live overrides, and confirm the full config keeps the embedded template structure with updated live values.
- Verify headless `/trace /blank /screenshot /exit`, and confirm the saved PNG keeps the blank background composition without dynamic metric content.
- Verify one headless run that supplies explicit output filenames such as `/trace:custom_trace.txt`, `/dump:custom_dump.txt`, and `/screenshot:custom_screenshot.png`, and confirm only the requested paths are updated.
- Verify one headless `/trace /default-config /layout:<name> /screenshot /exit` run when validating the built-in config, and confirm the screenshot and trace use the requested named layout without editing `config.ini`.
- Verify one headless `/trace /default-config /edit-layout /screenshot /exit` run when validating layout-guide rendering, and confirm the screenshot includes the layout edit guides.
- Verify one headless `/trace /default-config /edit-layout:metric_list /screenshot /exit` run when validating widget-local metric-list guides, and confirm the screenshot includes the metric-list outline plus its `label_width` guide without needing live hover.
- Verify one headless `/trace /default-config /edit-layout:throughput /screenshot /exit` run when validating the throughput widget-local guides, and confirm the screenshot includes the throughput outline plus its `axis_padding` guide at the plot-left edge and its `header_gap` guide at the header/graph boundary without needing live hover.
- Verify widget-local edit guides through an interactive `/edit-layout` UI run, hover `metric_list`, `throughput`, and `drive_usage_list`, and confirm the widget outline plus the `label_width`, `axis_padding`, `header_gap`, `activity_width`, `free_width`, drive-usage `header_gap`, and drive-usage `row_gap` guides appear inside those widgets and drag live.
- Verify headless `/trace /default-config /edit-layout:horizonatal-sizes /screenshot /exit` and `/trace /default-config /edit-layout:vertical-sizes /screenshot /exit` when validating size-ruler grouping and numbering, and inspect the trace for consecutive `renderer:layout_similarity_group` ordinals that match the visible rulers.
- When `/scale:<value>` is involved, confirm the screenshot uses the expected multiplied pixel dimensions while preserving the same logical composition.
- For fake-mode changes, verify both interactive `/fake` runs and headless `/fake /exit` runs, confirm that editing the selected fake file changes the next one-second refresh without touching live providers, and verify one run with `/fake:custom_fake.txt`.
- Verify `/blank /fake` fails before startup.
