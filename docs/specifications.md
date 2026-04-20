# System Telemetry

This document owns user-visible product behavior.
See also: [docs/layout.md](layout.md) for config language and section ownership, [docs/diagnostics.md](diagnostics.md) for diagnostics CLI behavior, [docs/build.md](build.md) for build and setup, and [docs/architecture.md](architecture.md) for code structure.

## Overview

System Telemetry is a compact real-time Windows dashboard for a small secondary display. It presents CPU, GPU, network, storage, and clock data in a static card layout that favors glanceable values, restrained chrome, and stable composition over dense detail.

The dashboard uses only Windows-native telemetry plus supported vendor APIs. It does not depend on LibreHardwareMonitor or OpenHardwareMonitor.

## Runtime Configuration Behavior

- The runtime loads the embedded `resources/config.ini` defaults first, then overlays `config.ini` beside `SystemTelemetry.exe` when that file exists.
- The embedded template remains the shipped default source, while [resources/config.ini](../resources/config.ini) remains the maintained example and spelling authority for config entries.
- The runtime treats window size, placement, and UI geometry as logical units and converts them to device pixels through either the current monitor DPI scale or the active explicit scale override.
- When the configured target monitor is temporarily unavailable, the runtime keeps watching for it and restores the saved placement there once the monitor becomes enumerable.
- `Reload Config` reapplies live configuration immediately without restarting.
- `Save Config` writes only the live values that differ from the loaded executable-side config state, preserving unchanged explicit overrides and unknown lines.
- When `Save Config` creates a new executable-side `config.ini`, it writes only the values that differ from the embedded defaults.
- `Save Full Config To...` exports a full config in the embedded-template shape with live values filled in.
- Save and export omit runtime-only placeholder metric metadata such as `nothing`, even when metric-list bindings still reference that placeholder id.
- If the executable-side `config.ini` is not writable, `Save Config` completes through the elevated helper path instead of relying on file virtualization.
- `Save Config` persists live placement, runtime network selection, runtime storage-drive selection, and any in-memory layout-edit changes that belong to the current edit session, then ends layout-edit mode when that mode is active.
- `Config To Display` computes a fitted explicit scale for the chosen display, resets placement to the display origin, writes `telemetry_blank.png`, updates the live config, and applies that blank image as the display wallpaper.

## Dashboard Composition And Rendering

- The active named layout selects one static dashboard composition at a time.
- The renderer resolves card and widget rectangles after config load or reload and keeps rendering in those resolved coordinates until the next reload.
- The dashboard content comes from cards named in the active layout, and each card may show an optional title, optional icon, and a configured widget composition.
- Cards with no title and no icon reserve no header area.
- Layout references may reuse another card's inner layout without duplicating its definition.
- The storage drive list uses the resolved runtime drive selection. An empty persisted drive selection means all currently available fixed drives.
- CPU and GPU cards each show a large segmented load gauge plus supporting metric rows.
- Network and storage cards show retained-history throughput plots with current value headers.
- The storage card also shows per-drive activity and usage rows.
- The time card shows a dominant time readout plus the local date.
- Metric labels and displayed units come from the metric registry defined in config, while dump units stay on the dump contract described in [docs/diagnostics.md](diagnostics.md).
- The UI style stays high-contrast and minimal: dark background, bright foreground text, restrained separators, rounded cards, compact headers, and shared visual rhythm across comparable cards.
- CPU and GPU gauges share one fitted gauge size within the active layout even when their surrounding cards differ in height.
- Metric rows and usage bars render as rounded horizontal fills, throughput widgets render scrolling retained-history lines with shared time markers, and drive activity renders as stacked whole-segment indicators.
- Rendering rejects non-finite telemetry values by treating them as unavailable rather than propagating invalid math into layout or draw paths.
- Blank rendering mode keeps the dashboard chrome, headers, static labels, device names, and empty tracks while omitting dynamic metric text, time, date, plot lines, leaders, peak ghosts, gauge fill, and drive-activity or usage fill.

## Runtime Controls And Menus

- Right-clicking the dashboard opens a popup menu with move, raise, reload, save, layout, scale, network, storage-drive, config-to-display, auto-start, diagnostics, and exit actions.
- The `Diagnostics` submenu exposes the same dump, screenshot, and full-config export formats used by the diagnostics subsystem.
- The tray icon exposes the same action set as the dashboard menu.
- The dashboard uses normal window Z-order behavior; `Bring On Top` raises it when needed.
- Double-clicking the tray icon performs `Bring On Top`.
- The tray menu shows `Bring On Top` as its default action.
- Move mode keeps the dashboard attached to the pointer until placement completes and overlays the current monitor name, effective scale, and logical relative coordinates inside the same frame as the dashboard.
- The `Layout` submenu lists configured named layouts, marks the active layout, applies a new selection immediately, and repaints the dashboard before any modeless layout-editor refresh work runs.
- The `Scale` submenu offers the default DPI-derived scale, maintained preset scales, and a custom numeric scale dialog. Changing scale applies immediately.
- The `Network` submenu lists runtime IPv4-capable adapter candidates, marks the active selection, and applies a new choice immediately.
- The `Storage drives` submenu lists runtime drive candidates, keeps checkbox state for the current selection, and reapplies rendering and telemetry immediately when the selection changes.

## Layout-Edit Behavior

- `Edit layout` toggles interactive layout-edit mode from the popup menu, and the command line can also start the dashboard in that mode for live UI or screenshot diagnostics.
- Layout-edit mode stays active across move mode, layout changes, scale changes, config reload, and runtime network or storage selection changes. It ends only when the user explicitly turns it off or when `Save Config` or `Config To Display` completes successfully.
- While layout-edit mode is active, the renderer shows container guides, the hovered widget outline, supported widget-local guides, and matching edit cursors.
- Hovering actionable text, card chrome, bars, metric rows, reorder handles, or widget-local geometry exposes the matching highlight and edit affordance for that target class.
- While the editor window is above the dashboard, selecting a tree node also highlights the matching split guide, widget guide, gap anchor, or text anchor for that config target, and shared gap or ring-stroke targets add the matching widget, card, or dashboard outline.
- Hovering actionable targets also shows a standard Win32 tooltip whose first line matches the edited config shape and whose second line uses the shared localized description for that target.
- Spurious mouse-leave notifications that arrive while the pointer is still inside the dashboard do not clear or rebuild layout-edit hover state until the next real pointer movement or explicit hover refresh.
- Right-clicking an actionable target prepends one focused `Edit ...` action for that exact target.
- The modeless `Edit Configuration` window stays separate from the dashboard window, exposes a config-ordered tree plus a live editor pane, previews valid edits immediately, refreshes its tree contents without painting an intermediate empty or partially rebuilt tree, keeps only the current edit session inside its save or discard boundary, and reopens at its last user-moved on-screen position.
- Bringing the `Edit Configuration` window to the foreground also raises the dashboard directly behind it in Z-order so the editor and dashboard stay visually paired, but dashboard-driven refreshes do not raise the editor window unless the user brings it forward.
- The editor supports filtering, per-field revert, config-local descriptions, and specialized editors for numeric values, fonts, colors, metrics, weight pairs, and metric-list row ordering.
- Metric leaves whose ids begin with `board.temp.` or `board.fan.` also expose a live `Binding` selector for the matching board-sensor mapping.
- The board-metric `Binding` selector keeps the last discovered provider sensor-name list available for config editing even if a later live board sample omits that metadata.
- Multiple logical board metrics can bind to the same provider sensor name, and each bound row shows that same live value.
- Metric-list widgets support row reorder handles and add-row affordances.
- Gauge, throughput, metric-list, drive-usage, text, card-chrome, dashboard-spacing, and container-split targets all stay editable through the shared layout-edit interaction model rather than through one-off editors.
- While the editor window is above the dashboard, covered dashboard regions stay mouse-transparent to the editor and suppress dashboard hover, tooltip, and cursor updates for those covered points.
- Closing the editor window closes only the modeless editor window, clears any tree-selection highlight from the dashboard, and keeps layout-edit mode active.
- Turning off layout-edit mode uses the shared unsaved-session prompt with save, discard, and cancel outcomes.
- Turning off layout-edit mode, exiting the app, or reloading config while the edit session is dirty always gives the user an explicit save-or-discard choice before destructive loss of the edit-session state.

## Telemetry And Content Behavior

- CPU content includes model name, load, clock, RAM usage, and any requested board temperature or fan metrics that resolve successfully through the active board provider.
- GPU content includes model name, load, dedicated VRAM usage, total dedicated VRAM capacity, and AMD vendor metrics such as temperature, clock, and fan speed when available.
- If a vendor provider is unavailable or unsupported, the dashboard stays running and shows those provider-owned values as unavailable instead of failing the app.
- Network content shows current upload and download throughput plus a footer line with the selected adapter name and IPv4 address when available.
- Storage throughput uses system-wide disk I/O counters, while per-drive rows use the currently selected drive set.
- Layout metric references are the only source of truth for which logical board metrics are requested from the board provider.
- The board mapping section connects those logical names to provider-specific sensor names, but the user-visible effect is simply that bound board metrics resolve when the mapped sensor exists.

## Refresh, Units, And Instance Behavior

- The runtime updates the shared telemetry snapshot on a 0.5 second cadence.
- CPU, GPU, network, storage, drive activity, retained histories, and the clock all refresh from that shared cadence.
- Retained histories feed throughput plots plus recent-peak or recent-max overlays for supported widgets.
- Dashboard formatting uses the configured metric display definitions, while dump serialization keeps its own stable machine-facing unit contract.
- The dashboard runs as a single-instance application. Starting a new UI instance closes the existing one and continues startup in the replacement instance.
- Headless diagnostics `/exit` runs are outside that normal single-instance replacement path; diagnostics-specific behavior is defined in [docs/diagnostics.md](diagnostics.md).
