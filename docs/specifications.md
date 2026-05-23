# CaseDash

This document owns general user-visible product behavior.
See also: [docs/glossary.md](glossary.md) for project terminology, [docs/hardware.md](hardware.md) for supported hardware providers, [docs/layout_edit.md](layout_edit.md) for layout-edit mode and editor behavior, [docs/layout.md](layout.md) for config language and section ownership, [docs/theme_configuration.md](theme_configuration.md) for theme selection and derived color behavior, [docs/diagnostics.md](diagnostics.md) for diagnostics CLI behavior, [docs/build.md](build.md) for build and setup, and [docs/architecture.md](architecture.md) for code structure.

## Overview

CaseDash is a compact real-time Windows dashboard for a small secondary display. It presents CPU, GPU, network, storage, and clock data in a static card layout that favors glanceable values, restrained chrome, and stable composition over dense detail.

The dashboard uses only Windows-native telemetry plus supported hardware-provider APIs. It does not depend on generic hardware-monitoring suites.

## Runtime Configuration Behavior

- The runtime loads the embedded `resources/config.ini` defaults first, then overlays `config.ini` beside `CaseDash.exe` when that file exists.
- The executable manifest declares UTF-8 as the process active code page, and runtime Win32 A API text boundaries use the same narrow UTF-8 text as the app model.
- The embedded template remains the shipped default source, while [resources/config.ini](../resources/config.ini) remains the maintained example and spelling authority for config entries.
- The runtime treats window size, placement, and UI geometry as logical units and converts them to device pixels through either the current monitor DPI scale or the active explicit scale override. Resolved display scales are rounded to three decimal places before window sizing.
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
- `Save Config` treats `display.wallpaper` as committed fullscreen wallpaper ownership. If the live placement no longer fills the configured monitor at `(0,0)`, `Save Config` writes `wallpaper = ` and clears the previously committed CaseDash-owned Windows wallpaper only after the config save succeeds.
- `Export Full Config...` writes the same normalized display wallpaper ownership state as a config export, but it does not clear or apply Windows wallpaper because it does not commit runtime display ownership.
- `Configure Display` offers one section per display. A display whose aspect ratio matches the active layout has one fullscreen entry labeled `<display name> full screen`; a nonmatching display has either top and bottom entries or left and right entries labeled `<display name> top|bottom|left|right`.
- Each real `Configure Display` entry shows a schematic icon before the text. The icon preserves the display aspect ratio as a simple rectangle and shades the region occupied by the CaseDash window for fullscreen, top, bottom, left, or right placement.
- Fullscreen display configuration saves the display origin, an explicit fitted scale, `wallpaper = casedash_blank.png`, and `autohide =`, renders the blank wallpaper image, applies it to the selected monitor, and clears the previous committed CaseDash-owned monitor wallpaper when switching ownership.
- Edge display configuration saves the selected monitor, explicit fitted scale, logical edge placement, `wallpaper = `, and `autohide = top|left|bottom|right` for the selected edge without rendering or applying a new wallpaper. It clears the previous committed CaseDash-owned wallpaper, including same-monitor wallpaper ownership from a previous fullscreen configuration.
- Applying a configure-display placement live uses the computed physical target rectangle for that placement, including exact right and bottom monitor-edge anchoring when the saved rounded logical coordinate cannot round-trip to the same pixel. If a currently visible hover titlebar cannot fit at the new fullscreen or edge placement, the titlebar frame is hidden before the target window rectangle is applied.
- `Configure Display` marks the active committed entry with a tinted schematic icon background when the last committed display config targets the same monitor rectangle, explicit scale, logical position, expected wallpaper value, and expected autohide value for that entry. Unsaved move, resize, or scale changes do not move the active marker until `Save Config` or `Configure Display` commits them. The native checkbox column is not used for configure-display placement rows.
- `[display] autohide` is empty by default. Non-empty values are meaningful only for `top`, `left`, `bottom`, or `right`; other values are loaded as text but do not activate runtime autohide behavior.

## Dashboard Composition And Rendering

- The active named layout selects one static dashboard composition at a time.
- The active named theme selects the base color tokens used by derived dashboard colors.
- The live window, tray, and owned dialog icons are rendered programmatically from the active resolved theme colors, with the icon background matching the dashboard card fill composited over the app background; the embedded `resources/app.ico` carries only `16`, `32`, and `64` pixel fallback frames for Windows shell use before runtime theme application and by executable metadata.
- The renderer resolves card and widget rectangles after config load or reload and keeps rendering in those resolved coordinates until the next reload.
- The dashboard content comes from cards named in the active layout, and each card may show an optional title, optional icon, and a configured widget composition.
- Cards with no title and no icon reserve no header area.
- Layout references may reuse another card's inner layout without duplicating its definition.
- The storage drive list uses the resolved runtime drive selection. An empty persisted drive selection means all currently available fixed drives.
- CPU and GPU cards each show a large segmented load gauge plus supporting metric rows.
- Network and storage cards show retained-history throughput plots with current value headers. Each card pair shares a vertical graph maximum computed from compact one-second retained-history values and the live leader, not from instantaneous raw samples.
- Throughput graph guide lines use 5-unit spacing on small scales and 50-unit spacing once the shared graph maximum is above 50.
- The storage card also shows per-drive activity and usage rows.
- The time card shows a dominant configured-format time readout plus the configured-format local date. The shipped formats are `HH:MM` for time and `YYYY-MM-DD` for date.
- Metric labels and displayed units come from the metric registry defined in config, while snapshot dump units stay on the snapshot dump contract described in [docs/diagnostics.md](diagnostics.md).
- The UI style stays high-contrast and minimal: dark background, bright foreground text, restrained separators, rounded cards, compact headers, and shared visual rhythm across comparable cards.
- CPU and GPU gauges share one fitted gauge size within the active layout even when their surrounding cards differ in height.
- Metric rows and usage bars render as rounded horizontal fills, throughput widgets render scrolling retained-history lines with shared time markers, and drive activity renders as stacked whole-segment indicators.
- In the live window, data-driven fills and throughput plots interpolate across the 250 ms telemetry cadence. Snapshot text, card chrome, labels, layout geometry, and edit affordances update only at snapshot or interaction boundaries.
- Deterministic renders, including blank mode, saved screenshots, layout guide sheet output, app icon output, and offscreen validation renders, draw target snapshot values directly without live interpolation.
- Metric-list rows can show a small right-aligned annotation above the bar when the resolved metric supplies one; `gpu.fps` uses this annotation for the cleaned presenting application name, or a warning-colored `!admin` indicator when only the presenting application name needs elevated access. When the FPS application name and FPS value share too little row width, the application name is shortened with a middle `...` while preserving the first letters and final letter so the value text remains unobscured.
- Palette colors include alpha. Gauge peak segments and metric-list recent-peak markers use the shared peak ghost color from the palette, and short permission-required metric indicators use the shared warning color.
- Metric rows and gauges draw only their background track when a metric value is unavailable or fully permission-gated; a fully permission-gated value displays the warning-colored short text `!admin` instead of `N/A`.
- Rendering rejects non-finite telemetry values by treating them as unavailable rather than propagating invalid math into layout or draw paths.
- Blank rendering mode keeps the dashboard chrome, headers, static labels, device names, and empty tracks while omitting dynamic metric text, time, date, plot lines, leaders, peak ghosts, gauge fill, and drive-activity or usage fill.

## Runtime Controls And Menus

- Right-clicking the dashboard opens a popup menu with `Move`, `Bring to Front`, `Devices`, `Theme`, `Layout`, `Edit Layout`, `Display`, `Start with Windows`, `About CaseDash`, and `Exit` actions.
- The `Display` submenu contains `Configure Display` and `Scale`; `Configure Display` lists enabled display placement entries separated by display section, and `Scale` offers the default DPI-derived scale, maintained preset scales, and a custom numeric scale dialog. Scale entries show the scale percentage and resulting dashboard window size as `<percent>% - <width>x<height>`.
- The `Devices` submenu contains `GPU`, `Network`, and `Storage Drives`.
- The `Edit Layout` submenu contains the checked `Edit Layout` mode toggle, `Layout Editor...`, and `Save Config`.
- Holding Alt while opening the dashboard or tray popup menu adds an `Advanced` submenu with `Run as administrator`, `Reload Config`, `Save Config`, `Export Full Config...`, `Export Snapshot Dump...`, `Save Screenshot...`, and `Save Layout Guide Sheet...`.
- `Run as administrator` restarts the dashboard through the Windows elevation prompt, preserves the current command-line diagnostics switches, and closes the current instance only after the elevated relaunch starts. The item is checked and disabled when the current dashboard is already elevated.
- The tray icon exposes the same action set as the dashboard menu.
- `About CaseDash` shows the current themed app icon in a square slot, compiled version, build kind, and commit when available.
- The dashboard uses normal window Z-order behavior; `Bring to Front` raises it when needed.
- When `[display] autohide` names a screen edge and the live dashboard client rectangle exactly matches the configured monitor's computed edge placement, the dashboard stays topmost and behaves as an auto-hide drawer. Every drawer show reasserts the dashboard at the top of the topmost z-order. Moving or resizing the dashboard leaves the config value intact but disables both topmost and drawer behavior until the live client rectangle again matches the computed placement pixel for pixel.
- Active autohide hides the dashboard with a native 200 ms slide animation after the pointer has been outside the dashboard for 500 ms. While hidden, an invisible 2 px trigger band on the configured edge of the configured monitor reopens the dashboard with the same native slide animation.
- The installer completion launch starts the dashboard with the same front-and-focus behavior as `Bring to Front`.
- Canceling the MSI setup from a setup wizard page and confirming exit closes the installer immediately.
- The MSI setup UI uses CaseDash-branded dialog and banner artwork. The welcome dialog bitmap uses the WiX dialog bitmap size and background, places the generated dark_cyan app icon and black `CaseDash` wordmark on the left side, and leaves the right-side WiX text area unobscured. The banner bitmap uses the WiX banner bitmap size, keeps a uniform pale background above a blue underline, and places a separately generated dark_cyan app icon at native size in the right-hand area.
- The MSI registration uses the embedded CaseDash app icon for the Windows installed-apps entry.
- Installing a CaseDash MSI replaces any existing CaseDash MSI registration, including newer, older, and same-version packages.
- MSI upgrade preserves runtime-owned files beside `CaseDash.exe`, including the executable-side `config.ini` and `casedash_blank.png`, while updating the installed executable.
- MSI uninstall closes the running dashboard when needed and removes the install directory tree.
- Double-clicking the tray icon performs `Bring to Front`.
- The tray menu shows `Bring to Front` as its default action.
- Move mode keeps the dashboard attached to the pointer until placement completes, clamps the active pointer offset inside the current dashboard bounds after DPI or size changes, and overlays the current monitor name, monitor default scale percentage, logical relative coordinates, dashboard size, and current app scale percentage inside the same frame as the dashboard. Completing move mode updates the live display monitor and logical position once, so later layout or theme changes keep the placed position.
- Dragging any invisible dashboard corner resizes the dashboard while preserving the active layout aspect ratio and keeping the opposite corner fixed on screen. When the hover titlebar is visible, the top-left and top-right resize zones extend upward through the corresponding titlebar corners. When the live dashboard client rectangle exactly matches the configured display-menu fullscreen or edge placement on the configured monitor, resize zones are inactive and keep the normal cursor. Resize mode uses the same placement overlay as move mode with a `Resize Mode` title, stores the rounded explicit display scale plus monitor and logical position once when the pointer is released, and leaves the in-memory wallpaper value unchanged until `Save Config` or `Configure Display` commits display wallpaper ownership.
- The `Layout` submenu lists configured named layouts, marks the active layout, applies a new selection immediately, and repaints the dashboard before any modeless `Edit Configuration` window refresh work runs.
- The `Theme` and `Layout` submenus list configured named sections as `name - description` when a description is configured, mark the active entry, apply a new selection immediately, and repaint the dashboard before any modeless `Edit Configuration` window refresh work runs. The `Theme` submenu appears immediately before `Layout`.
- The `Scale` submenu changes scale immediately.
- The `GPU` submenu lists unique runtime GPU adapter candidates, numbers candidates with duplicate hardware descriptions in stable PCI-address order, marks the active selection, and applies a new choice immediately.
- The `Network` submenu lists runtime IPv4-capable adapter candidates, marks the active selection, and applies a new choice immediately.
- The `Storage Drives` submenu lists runtime drive candidates, keeps checkbox state for the current selection, and reapplies rendering and telemetry immediately when the selection changes.
- The `Start with Windows` command installs machine-wide logon startup for the dashboard UI through `HKLM\Software\Microsoft\Windows\CurrentVersion\Run` and installs and starts the machine-wide `CashDashService` service used for privileged collection. The command is checked only when the Run entry targets the current executable, the service binary path targets the current executable, and the service is running. Disabling auto-start removes the Run entry, stops and deletes the service, and treats an already-absent or SCM-pending-delete service as cleaned up. Enabling or disabling auto-start uses the elevated helper path when administrator rights are required, and failed enable attempts roll back partial service or Run registration.

## Hover Titlebar

- Hovering the dashboard or the virtual titlebar band directly above it shows a Windows titlebar strip titled `CaseDash` while keeping the rendered dashboard content in the same screen rectangle.
- When supported by Windows DWM, the visible titlebar state asks Windows for native rounded top-level window corners, the system default border, the system default caption and text colors, and dark-mode-aware frame rendering. The custom hover strip preserves rounded top corners while it covers the native caption, and it uses a modern light or dark Windows caption palette for its background and glyphs. Unsupported DWM chrome attributes fail open and leave the existing rectangular strip behavior.
- The visible titlebar strip uses the dashboard width and stays hidden only when there is not enough space above the dashboard on the same monitor or when the dashboard client is exactly stuck to the monitor's top, left, right, or bottom edge. Native side or bottom frame overhang by itself does not suppress the titlebar.
- Active autohide suppresses the hover titlebar so only the dashboard content participates in drawer open and close animation.
- Leaving the dashboard, titlebar, or virtual titlebar band starts a 500 ms delayed hide of the strip unless a titlebar control dropdown, titlebar menu, close action, or an active move or resize placement with the titlebar already visible is in progress. Re-entering the hover area before the delay expires keeps the strip visible, and leaving again restarts the delay.
- The left side of the titlebar shows a small CaseDash icon button before the `CaseDash` title; left-clicking it opens the same full menu as right-clicking the dashboard.
- Right-aligned titlebar controls appear left to right as a theme dropdown, a layout dropdown, an edit-layout button, a display setup button, and a close button.
- As titlebar width shrinks, existing titlebar controls are repositioned and hidden without recreating native dropdown HWNDs. The titlebar drops the theme dropdown, layout dropdown, edit-layout button, and display setup button in that order; the close button remains visible, and the app icon hides only when it would collide with the close button.
- Hovering a titlebar control shows a localized tooltip for that control without rebuilding the titlebar or interrupting layout-edit dashboard tooltip transitions.
- The layout dropdown is a native Windows dropdown, shows layout names only, reflects the active layout, and applies a new layout through the same behavior as the `Layout` submenu.
- The theme dropdown is a native Windows dropdown, shows theme names with spaces such as `dark cyan`, reflects the active theme, and applies a new theme through the same behavior as the `Theme` submenu.
- The edit-layout button is a custom gray titlebar button with a minimal guide-crosshair glyph, shares hover and pressed feedback with the display setup button, toggles the same `Edit Layout` mode as the popup menu, and uses the native selected background shared by configure-display schematic icons while layout-edit mode is active.
- The display setup button is a custom gray titlebar button with a minimal monitor glyph, shares hover and pressed feedback with the close button, and opens the same target-display entries as `Display` > `Configure Display`.
- The close button is a custom `X` button, uses the native Windows close-button hover or pressed background when available, and exits through the same save-or-discard prompt used by `Exit` when layout-edit changes are unsaved.
- Dragging empty titlebar space starts dashboard move mode with the usual monitor, scale, coordinate, and size overlay, and the overlay prompts `Release to place` because releasing the pointer completes placement. An already visible titlebar remains pinned until active move or resize placement ends, even if normal hover eligibility temporarily fails; during resize the existing titlebar probe and controls are repositioned with immediate redraw suppressed, copied titlebar pixels are discarded, and the affected titlebar windows are synchronously repainted during the drag. Move-mode entry points that begin while the titlebar is hidden keep it hidden and prompt `Left-click to place`.

## Telemetry And Content Behavior

- CPU content includes model name, load, clock, RAM usage, and any requested board temperature or fan metrics that resolve successfully through the active board provider.
- GPU content includes model name, load, dedicated VRAM usage, total dedicated VRAM capacity, and hardware-provider metrics such as temperature, clock, fan speed, and game FPS when available from the active GPU provider. GPU fan speed falls back to a board-provider GPU fan only when the GPU provider does not supply fan RPM. Intel GPU temperature falls back to the resolved board CPU temperature only when the selected Intel GPU provider does not supply a native temperature. GPU telemetry selects the provider from the configured unique non-software adapter selection name, or the first unique non-software adapter when no GPU adapter is configured. If no supported GPU provider matches, the unsupported GPU provider keeps provider-owned metrics unavailable and supplies only the shared presented-FPS metric when ETW access and present events are available.
- Presented FPS is the default `gpu.fps` source when available. Process selection prefers a presenting process with dominant GPU Engine 3D usage over background presenters, keeps the current presenter through brief count ties or near-ties, and reports unavailable when a dominant 3D application is visible but matching present events are not.
- Board telemetry selects the provider from the baseboard manufacturer, reads supported board temperatures and fan speeds through the active provider, and uses an unsupported-board provider with unavailable values when no supported board provider matches.
- If a hardware provider is unavailable or unsupported, the dashboard stays running and shows those provider-owned values as unavailable instead of failing the app.
- Network content shows current upload and download throughput plus a footer line with the selected adapter name and IPv4 address when available.
- Storage throughput uses system-wide disk I/O counters, while per-drive rows use the currently selected drive set.
- Throughput graph history stores 30 one-second averaged body points plus the last four raw 250 ms samples used for the live leader. Graph maximum selection uses those phase-aware displayed values, including the live leader, and does not use instantaneous raw endpoint spikes.
- Layout metric references are the only source of truth for which logical board metrics are requested from the board provider.
- The board mapping section connects those logical names to provider-specific sensor names. Empty CPU, GPU, and system bindings use first-use auto-detection from the active provider's sensor names; otherwise, bound board metrics resolve when the mapped sensor exists. The `gpu` board fan binding is requested by `gpu.fan` as a fallback source when the selected GPU provider does not expose fan RPM. The `cpu` board temperature binding is requested by `gpu.temp` so selected Intel GPUs can fall back to CPU package temperature when no native Intel GPU temperature exists. Supported provider details are defined in [docs/hardware.md](hardware.md).

## Refresh, Units, And Instance Behavior

- The telemetry runtime owns collection on a 250 ms cadence, skips missed intervals after process stalls or machine sleep, and publishes each new snapshot to the dashboard when collection finishes.
- The telemetry cadence is the shared duration used by live dashboard animation, so a visual transition completes as the next steady-state telemetry snapshot becomes due.
- CPU, GPU, network, storage, drive activity, retained histories, and the clock all refresh from that telemetry-owned cadence.
- The built-in synthetic telemetry source is static for deterministic one-shot diagnostics and live for normal `/fake` dashboard runs. Live fake runs advance one synthetic reading on the same 250 ms telemetry cadence as real collection, including the retained-throughput smoothing used by network and storage charts.
- The dashboard redraws after receiving a new telemetry snapshot instead of driving collection from the UI message loop.
- Retained histories feed throughput plots plus recent-peak or recent-max overlays for supported widgets.
- Dashboard formatting uses the configured metric display definitions, while snapshot dump serialization keeps its own stable machine-facing unit contract.
- The dashboard runs as a single-instance application. Starting a new UI instance closes the existing one and continues startup in the replacement instance.
- Starting with `/bring-to-front` uses the normal UI startup path and raises the dashboard after creating the tray icon. Elevated UI relaunches include this switch and briefly retry the foreground raise after startup.
- Headless diagnostics `/exit` runs are outside that normal single-instance replacement path; diagnostics-specific behavior is defined in [docs/diagnostics.md](diagnostics.md).
