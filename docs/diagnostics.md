# System Telemetry diagnostics

This document is the single maintained source of truth for diagnostics command behavior, validation commands, and diagnostics-output inspection guidance.

## Command-line switches

- `/trace[:path]` enables continuous trace logging to `telemetry_trace.txt` in the current working directory, or to the optional target path.
- `/dump[:path]` writes a machine-parseable snapshot dump to `telemetry_dump.txt` in the current working directory, or to the optional target path.
- `/screenshot[:path]` writes a rendered dashboard PNG to `telemetry_screenshot.png` in the current working directory, or to the optional target path.
- `/reload` forces a config reload through the normal live-dashboard reload path before headless diagnostics outputs are exported.
- `/exit` runs diagnostics as a one-shot headless export path instead of starting the dashboard UI.
- `/fake[:path]` replaces live telemetry collection with periodic reads from `telemetry_fake.txt` in the current working directory, or from the optional fake dump path.
- `/layout:<name>` overrides `display.layout` for the current process so diagnostics can validate a named layout without editing `config.ini`.
- `/blank` switches screenshot rendering into a blank background mode that keeps static dashboard chrome and static text such as CPU and GPU names while omitting dynamic metric text, time, date, plots, leaders, peak ghosts, gauge fill, and drive activity or usage fill.
- `/scale:<value>` multiplies headless `/screenshot /exit` render size, including all measured layout geometry, and accepts fractional values such as `1.5`.
- `/blank` cannot be combined with `/fake`.

## Output files

- Without an explicit path, `/trace`, `/dump`, and `/screenshot` write in the current working directory using their default filenames.
- With an explicit path, each switch writes the same content format to that requested path instead of the default file.
- Without an explicit path, `/fake` reads `telemetry_fake.txt` from the current working directory.
- With an explicit path, `/fake` reads that requested dump file instead of the default fake file.
- Relative diagnostics paths resolve from the current working directory.
- The UI `Diagnostics` submenu uses a standard Save dialog and defaults `Save dump to...` and `Save screenshot to...` to the current working directory with the same default file names used by `/dump` and `/screenshot`.
- Trace output appends plain UTF-8 text without a BOM and uses the prefix format `[trace yyyy-mm-dd hh:mm:ss.mmm]`.
- Dump output overwrites with a stable text format that can be copied directly into the default fake file or a `/fake` target file.
- Screenshot output overwrites with only the rendered dashboard PNG.

## Runtime behavior

- Without `/exit`, the application starts the normal dashboard UI and keeps producing any requested diagnostics outputs while it runs.
- In UI-attached mode, trace logging continues for the process lifetime, while dump and screenshot outputs refresh once per second from the latest snapshot.
- With `/exit`, the application initializes telemetry from the normal runtime `config.ini`, performs the first update, optionally writes the requested outputs once, and exits without starting the GUI.
- With `/layout:<name>`, the application applies that named layout after loading `config.ini` and keeps that override active for the rest of the process, including `/reload` diagnostics runs.
- With `/reload /exit`, the application completes the normal first startup and update path, reloads config through the same live-dashboard logic, and exports outputs from the reloaded state.
- With `/fake`, the application skips live telemetry providers, loads the selected fake dump file immediately, and reloads it once per second while the process runs.
- With `/blank`, the application keeps the normal layout, panel chrome, card headers, CPU and GPU names, drive labels, and empty chart or bar tracks while suppressing dynamic metric rendering.

## Failures and trace coverage

- The diagnostics trace covers all diagnostics collection paths, not only vendor GPU integration.
- Trace lines for telemetry or vendor API calls should include returned status or result codes plus key sampled values that help explain missing metrics or failures.
- Diagnostics output failures such as trace, dump, or screenshot file-open or write failures must be written to the trace before any error dialog is shown.
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
- When validating default diagnostics paths, launch the executable from the intended working directory and confirm the default files land there.
- When validation commands specify diagnostics paths explicitly, point them somewhere under `build\` so trace, dump, screenshot, and fake files do not pollute the repository root.
- Verify UI-attached `/trace`, `/dump`, `/screenshot`, and `/trace /dump /screenshot`.
- Verify headless `/trace /dump /screenshot /exit` and `/trace /reload /screenshot /exit`, and confirm the process exits after the requested export path.
- Verify headless `/trace /blank /screenshot /exit`, and confirm the saved PNG keeps the blank background composition without dynamic metric content.
- Verify one headless run that supplies explicit output filenames such as `/trace:custom_trace.txt`, `/dump:custom_dump.txt`, and `/screenshot:custom_screenshot.png`, and confirm only the requested paths are updated.
- Verify one headless `/trace /layout:<name> /screenshot /exit` run, and confirm the screenshot and trace use the requested named layout without editing `config.ini`.
- When `/scale:<value>` is involved, confirm the screenshot uses the expected multiplied pixel dimensions while preserving the same logical composition.
- For fake-mode changes, verify both interactive `/fake` runs and headless `/fake /exit` runs, confirm that editing the selected fake file changes the next one-second refresh without touching live providers, and verify one run with `/fake:custom_fake.txt`.
- Verify `/blank /fake` fails before startup.
