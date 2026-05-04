# CaseDash

This document owns general user-visible product behavior.
See also: [docs/hardware.md](hardware.md) for supported hardware providers, [docs/layout_edit.md](layout_edit.md) for layout-edit mode and editor behavior, [docs/layout.md](layout.md) for config language and section ownership, [docs/theme_configuration.md](theme_configuration.md) for theme selection and derived color behavior, [docs/diagnostics.md](diagnostics.md) for diagnostics CLI behavior, [docs/build.md](build.md) for build and setup, and [docs/architecture.md](architecture.md) for code structure.

## Overview

CaseDash is a compact real-time Windows dashboard for a small secondary display. It presents CPU, GPU, network, storage, and clock data in a static card layout that favors glanceable values, restrained chrome, and stable composition over dense detail.

The dashboard uses only Windows-native telemetry plus supported hardware-provider APIs. It does not depend on generic hardware-monitoring suites.

## Runtime Configuration Behavior

- The runtime loads the embedded `resources/config.ini` defaults first, then overlays `config.ini` beside `CaseDash.exe` when that file exists.
- The embedded template remains the shipped default source, while [resources/config.ini](../resources/config.ini) remains the maintained example and spelling authority for config entries.
- The runtime treats window size, placement, and UI geometry as logical units and converts them to device pixels through either the current monitor DPI scale or the active explicit scale override.
- When the configured target monitor is temporarily unavailable, the runtime keeps watching for it and restores the saved placement there once the monitor becomes enumerable.
- `Reload Config` reapplies live configuration immediately without restarting.
- `Save Config` writes only the live values that differ from the loaded executable-side config state, preserving unchanged explicit overrides and unknown lines.
- For color values, `Save Config` compares the persisted color expression or literal text, so switching themes does not write derived color roles whose expressions are unchanged.
- Saved config text has no leading empty lines; retained comments and sections are separated by a single empty line when a new section is inserted after them.
- When `Save Config` creates a new executable-side `config.ini`, it writes only the values that differ from the embedded defaults.
- `Export Full Config...` exports a full config in the embedded-template shape with live values filled in.
- Save and export omit runtime-only placeholder metric metadata such as `nothing`, even when metric-list bindings still reference that placeholder id.
- If the executable-side `config.ini` is not writable, `Save Config` completes through the elevated helper path instead of relying on file virtualization.
- `Save Config` persists live placement, active theme selection, runtime network selection, runtime storage-drive selection, auto-detected board metric bindings, and any in-memory layout-edit changes that belong to the current edit session, then ends layout-edit mode when that mode is active.
- `Configure Display` computes a fitted explicit scale for the chosen display, resets placement to the display origin, writes `casedash_blank.png`, updates the live config, and applies that blank image as the display wallpaper.
- `Configure Display` marks a display entry with a checkbox when the live config already targets that display at `0,0` and the configured wallpaper path is non-empty, while still allowing that checked entry to be invoked again.

## Dashboard Composition And Rendering

- The active named layout selects one static dashboard composition at a time.
- The active named theme selects the base color tokens used by derived dashboard colors.
- The live window, tray, and owned dialog icons are rendered programmatically from the active resolved theme colors, with the icon background matching the dashboard card fill composited over the app background; the embedded `resources/app.ico` remains the Windows shell fallback used before runtime theme application and by executable metadata.
- The renderer resolves card and widget rectangles after config load or reload and keeps rendering in those resolved coordinates until the next reload.
- The dashboard content comes from cards named in the active layout, and each card may show an optional title, optional icon, and a configured widget composition.
- Cards with no title and no icon reserve no header area.
- Layout references may reuse another card's inner layout without duplicating its definition.
- The storage drive list uses the resolved runtime drive selection. An empty persisted drive selection means all currently available fixed drives.
- CPU and GPU cards each show a large segmented load gauge plus supporting metric rows.
- Network and storage cards show retained-history throughput plots with current value headers. Each card pair shares a vertical graph maximum computed from smoothed retained-history values, not from instantaneous current values.
- Throughput graph guide lines use 5-unit spacing on small scales and 50-unit spacing once the shared graph maximum is above 50.
- The storage card also shows per-drive activity and usage rows.
- The time card shows a dominant configured-format time readout plus the configured-format local date. The shipped formats are `HH:MM` for time and `YYYY-MM-DD` for date.
- Metric labels and displayed units come from the metric registry defined in config, while dump units stay on the dump contract described in [docs/diagnostics.md](diagnostics.md).
- The UI style stays high-contrast and minimal: dark background, bright foreground text, restrained separators, rounded cards, compact headers, and shared visual rhythm across comparable cards.
- CPU and GPU gauges share one fitted gauge size within the active layout even when their surrounding cards differ in height.
- Metric rows and usage bars render as rounded horizontal fills, throughput widgets render scrolling retained-history lines with shared time markers, and drive activity renders as stacked whole-segment indicators.
- Metric-list rows can show a small right-aligned annotation above the bar when the resolved metric supplies one; `gpu.fps` uses this annotation for the cleaned presenting application name, or a warning-colored `!admin` indicator when only the presenting application name needs elevated access. When the FPS application name and FPS value share too little row width, the application name is shortened with a middle `...` while preserving the first letters and final letter so the value text remains unobscured.
- Palette colors include alpha. Gauge peak segments and metric-list recent-peak markers use the shared peak ghost color from the palette, and short permission-required metric indicators use the shared warning color.
- Metric rows and gauges draw only their background track when a metric value is unavailable or fully permission-gated; a fully permission-gated value displays the warning-colored short text `!admin` instead of `N/A`.
- Rendering rejects non-finite telemetry values by treating them as unavailable rather than propagating invalid math into layout or draw paths.
- Blank rendering mode keeps the dashboard chrome, headers, static labels, device names, and empty tracks while omitting dynamic metric text, time, date, plot lines, leaders, peak ghosts, gauge fill, and drive-activity or usage fill.

## Runtime Controls And Menus

- Right-clicking the dashboard opens a popup menu with `Move`, `Bring to Front`, `Layout`, `Theme`, `Display`, `Devices`, `Edit Layout`, `Start with Windows`, `About CaseDash`, and `Exit` actions.
- The `Display` submenu contains `Configure Display` and `Scale`; `Configure Display` lists target displays, and `Scale` offers the default DPI-derived scale, maintained preset scales, and a custom numeric scale dialog.
- The `Devices` submenu contains `Network` and `Storage Drives`.
- The `Edit Layout` submenu contains the checked `Edit Layout` mode toggle, `Layout Editor...`, and `Save Config`.
- Holding Alt while opening the dashboard or tray popup menu adds an `Advanced` submenu with `Reload Config`, `Save Config`, `Export Full Config...`, `Export Snapshot Dump...`, `Save Screenshot...`, and `Save Layout Guide Sheet...`.
- The tray icon exposes the same action set as the dashboard menu.
- `About CaseDash` shows the current themed app icon in a square slot, compiled version, build kind, and commit when available.
- The dashboard uses normal window Z-order behavior; `Bring to Front` raises it when needed.
- The installer completion launch starts the dashboard with the same front-and-focus behavior as `Bring to Front`.
- Canceling the MSI setup from a setup wizard page and confirming exit closes the installer immediately.
- The MSI setup UI uses CaseDash-branded dialog and banner artwork. The welcome dialog bitmap uses the WiX dialog bitmap size and background, places the dark_cyan app icon and black `CaseDash` wordmark on the left side, and leaves the right-side WiX text area unobscured.
- The MSI registration uses the embedded CaseDash app icon for the Windows installed-apps entry.
- Installing a CaseDash MSI replaces any existing CaseDash MSI registration, including newer, older, and same-version packages.
- MSI upgrade preserves runtime-owned files beside `CaseDash.exe`, including the executable-side `config.ini` and `casedash_blank.png`, while updating the installed executable.
- MSI uninstall closes the running dashboard when needed and removes the install directory tree.
- Double-clicking the tray icon performs `Bring to Front`.
- The tray menu shows `Bring to Front` as its default action.
- Move mode keeps the dashboard attached to the pointer until placement completes, clamps the active pointer offset inside the current dashboard bounds after DPI or size changes, and overlays the current monitor name, effective scale, and logical relative coordinates inside the same frame as the dashboard.
- The `Layout` submenu lists configured named layouts, marks the active layout, applies a new selection immediately, and repaints the dashboard before any modeless layout-editor refresh work runs.
- The `Layout` and `Theme` submenus list configured named sections as `name - description` when a description is configured, mark the active entry, apply a new selection immediately, and repaint the dashboard before any modeless layout-editor refresh work runs. The `Theme` submenu appears immediately after `Layout`.
- The `Scale` submenu changes scale immediately.
- The `Network` submenu lists runtime IPv4-capable adapter candidates, marks the active selection, and applies a new choice immediately.
- The `Storage Drives` submenu lists runtime drive candidates, keeps checkbox state for the current selection, and reapplies rendering and telemetry immediately when the selection changes.
- The `Start with Windows` command installs machine-wide logon startup for the dashboard UI through `HKLM\Software\Microsoft\Windows\CurrentVersion\Run` and installs and starts the machine-wide `CashDashService` service used for privileged collection. The command is checked only when the Run entry targets the current executable and the matching service is currently running. Enabling or disabling auto-start uses the elevated helper path when administrator rights are required.

## Telemetry And Content Behavior

- CPU content includes model name, load, clock, RAM usage, and any requested board temperature or fan metrics that resolve successfully through the active board provider.
- GPU content includes model name, load, dedicated VRAM usage, total dedicated VRAM capacity, and hardware-provider metrics such as temperature, clock, fan speed, and game FPS when available from the active GPU provider. GPU telemetry selects the provider from the primary non-software adapter identity. If no supported GPU provider matches, the unsupported GPU provider keeps provider metrics unavailable and supplies only the shared presented-FPS metric when ETW access and present events are available.
- Presented FPS is the default `gpu.fps` source when available. Process selection prefers a presenting process with dominant GPU Engine 3D usage over background presenters, keeps the current presenter through brief count ties or near-ties, and reports unavailable when a dominant 3D application is visible but matching present events are not.
- Board telemetry selects the provider from the baseboard manufacturer, reads supported board temperatures and fan speeds through the active provider, and uses an unsupported-board provider with unavailable values when no supported board provider matches.
- If a hardware provider is unavailable or unsupported, the dashboard stays running and shows those provider-owned values as unavailable instead of failing the app.
- Network content shows current upload and download throughput plus a footer line with the selected adapter name and IPv4 address when available.
- Storage throughput uses system-wide disk I/O counters, while per-drive rows use the currently selected drive set.
- Layout metric references are the only source of truth for which logical board metrics are requested from the board provider.
- The board mapping section connects those logical names to provider-specific sensor names. Empty CPU and system bindings use first-use auto-detection from the active provider's sensor names; otherwise, bound board metrics resolve when the mapped sensor exists. Supported provider details are defined in [docs/hardware.md](hardware.md).

## Refresh, Units, And Instance Behavior

- The telemetry runtime owns collection on a 0.5 second cadence and publishes each new snapshot to the dashboard when collection finishes.
- CPU, GPU, network, storage, drive activity, retained histories, and the clock all refresh from that telemetry-owned cadence.
- The dashboard redraws after receiving a new telemetry snapshot instead of driving collection from the UI message loop.
- Retained histories feed throughput plots plus recent-peak or recent-max overlays for supported widgets.
- Dashboard formatting uses the configured metric display definitions, while dump serialization keeps its own stable machine-facing unit contract.
- The dashboard runs as a single-instance application. Starting a new UI instance closes the existing one and continues startup in the replacement instance.
- Starting with `/bring-to-front` uses the normal UI startup path and raises the dashboard after creating the tray icon.
- Headless diagnostics `/exit` runs are outside that normal single-instance replacement path; diagnostics-specific behavior is defined in [docs/diagnostics.md](diagnostics.md).
