# System Telemetry

This project is a compact, real-time system monitoring dashboard designed for a small 800x480 secondary display,
presenting only the telemetry that can be collected cleanly through Windows APIs or vendor APIs.
It combines CPU load, temperature, clock, fan speed, and RAM usage with GPU load, temperature, clock, fan speed, and VRAM usage,
alongside network activity, system-wide storage throughput, multi-drive storage status, and a clock, all organized into a balanced panel layout.
The design emphasizes glanceable visuals with large load indicators, concise numeric stats, and minimal clutter.

```text
┌──────────────────────────────┬───────────────────────────────┐
│           CPU                │             GPU               │
│  AMD Ryzen                   │  Radeon RX 6800               │
│  ○  2%                       │  ○  0%                        │
│  RAM     15.0 / 64 GB        │  VRAM    3.7 / 16 GB          │
│  Temp    67°C                │  Temp    45°C                 │
│  Clock   3.70GHz             │  Clock   29MHz                │
│  Fan     2400RPM             │  Fan     0RPM                 │
│  System Fan 900RPM           │                               │
├───────────────┬──────────────────────────────────┬───────────┤
│   Network     │             Storage              │   Time    │
│  Up   0.3MB/s │  Read  0.8MB/s  C: ████░░░░ 32%  │   10:43   │
│  Down 24MB/s  │  Write 1.2MB/s D: ██████░░ 44%   │ 2026-04-01│
│  ▒▒▒▒▒▒▒▒▒▒   │  ▒▒▒▒▒▒▒▒▒▒    E: ███░░░░░ 32%   │           │
│  ▒▒▒▒▒▒▒▒▒▒   │  ▒▒▒▒▒▒▒▒▒▒                      │           │
│  192.168.3.60 │                                  │           │
└───────────────┴──────────────────────────────────┴───────────┘
```

## Requirements

- Runtime prerequisites and developer setup are documented in [docs/build.md](build.md).
- The application must not depend on LibreHardwareMonitor or OpenHardwareMonitor.

## Configuration

### Configurable choices

Make the system configurable for display placement and the subset of data sources that require runtime choice.
Examples include:

- Display on which to show the system
- Relative position on the selected display
- Which network adapter to prefer
- Which storage drives to show in the storage usage list
- Which cards and widgets to show in each row
- Which named board temperature and fan metrics to render through the layout bindings

### Config sources and persistence

- At runtime, the application must first load an embedded default `config.ini` resource, then read `config.ini` from the same directory as `SystemTelemetry.exe` when that file exists and overlay its values on top of the embedded defaults.
- The embedded `resources/config.ini` template must remain the single maintained source of truth for config-file entries, and [docs/layout.md](layout.md) must remain the single maintained source of truth for config-language syntax, section ownership, and examples.
- The config parser must accept only the documented `resources/config.ini` key spellings and metric references.
- The config parser must commit parsed field values through the config schema's field setters so schema-owned field-local clamps apply uniformly to embedded defaults, executable-side overlays, and other parsed config sources.
- Before writing `config.ini` beside the executable, the application must verify that the process can write there; when it cannot, `Save Config` must prompt for elevation and complete the save through an elevated helper instance.
- The runtime executable must embed an application manifest that disables file virtualization so `config.ini` reads and writes target the executable-side file even when the app is installed under `Program Files`.
- The runtime executable must also opt into per-monitor DPI awareness so Windows does not bitmap-scale a finished low-resolution dashboard surface on scaled displays.
- `Save Config` must load the executable-side `config.ini` through the normal startup overlay path into a comparison copy, update only the keys whose live in-memory values differ, and write those updates back into the existing INI text so unchanged explicit overrides and unknown lines remain intact.
- When `Save Config` creates `config.ini` beside the executable for the first time, it must write only the keys whose live in-memory values differ from the embedded defaults.
- `Save Full Config To...` must export a complete config file by starting from the embedded `resources/config.ini` template text and updating every maintained config key with the live in-memory values so the exported file keeps the shipped line structure and comments.
- The runtime must rely on the embedded `resources/config.ini` template for shipped layout defaults.
- Embedded `resources/config.ini`, embedded `resources/localization.ini`, and executable-side `config.ini` inputs must be valid UTF-8 text, and invalid UTF-8 bytes must be rejected instead of being decoded through the active ANSI code page.
- The `[metrics]` section must define every metric id that `metric_list(...)`, `gauge(...)`, `throughput(...)`, and `drive_usage_list` presentation may bind, storing each metric as `<scale>,<unit>,<label>` where display style comes from the built-in dashboard metric metadata for that metric id and `*` means the renderer normalizes that metric against telemetry-provided scale data.
- The shipped `[metrics]` set must include throughput display ids for `network.upload`, `network.download`, `storage.read`, and `storage.write`, plus drive-list display ids for `drive.activity.read`, `drive.activity.write`, `drive.usage`, and `drive.free`.
- The layout-owned config state must include the `[board]` logical sensor bindings and the `[metrics]` metric-definition registry together with the rest of `LayoutConfig`, so layout-edit save and discard decisions apply to those sections as part of the same edit session.
- The `[display]` section must select the active dashboard layout by name through `display.layout`, and named dashboard size-and-card-placement definitions must live in `[layout.<name>]` sections with aspect-ratio names plus an optional `description` popup label suffix.
- The `[display]` section must store the dashboard render scale through `display.scale` as a fractional multiplier where `0` means the runtime uses the current monitor DPI scale.
- The shipped config template must define `5x3` as the default active layout, also include an experimental `3x5` portrait layout for the same panel resolution, and expose human-readable layout descriptions such as `5" 800x480 screen` for the layout popup.
- The config overlay path must replace parsed layout expressions during overlay, so `config.ini` overrides `[layout.<name>]` and `[card.*]` layout trees without duplicating cards or widgets.

### Layout and rendering behavior

- The configured `layout.window`, display `position`, and layout geometry/font sizes must be treated as logical units that are converted to native device pixels using either the configured `display.scale` override or the current monitor DPI scale before rendering.
- When `display.monitor_name` targets a monitor that is not yet available during login startup, display-topology churn, or a temporary unplug, the UI must keep watching for that configured monitor instead of locking in a fallback monitor placement, and once the monitor is available it must apply the saved logical position there.
- When `[display] wallpaper` is set to an image path, the runtime must resolve relative paths beside `SystemTelemetry.exe` and apply that image as the wallpaper for the configured display through Windows per-monitor wallpaper APIs during startup and config reload, retrying once the configured display becomes enumerable if startup monitor discovery races behind login or hotplug.
- The layout engine must resolve row, card, and widget coordinates once after config load or reload and keep rendering in those static coordinates until the config is reloaded again.
- Card-local layout expressions may reference another card id as a reusable sub-layout, and the renderer must substitute that referenced card's layout during layout resolution instead of flattening it during config parse.
- The list of rendered cards must come from layout config.
- Card titles and icons must be optional, and when a card specifies neither one the renderer must not reserve any card-header height for that card.
- The storage drive list must come from the resolved `[storage] drives` selection.
- When `[storage] drives` is empty, the storage telemetry selection flow must replace it with all currently available fixed drives before the renderer and telemetry consume the effective config.
- The dedicated widget sections must derive metric-list and drive-usage row heights from measured UI font metrics plus dedicated bar-height and vertical-gap settings, so font-size experiments preserve or intentionally retune the visual rhythm.
- When a `metric_list` or `drive_usage_list` widget has less vertical space than its full configured content needs, the renderer must keep each header, row, bar, and configured gap at its full configured height and crop any overflow at the bottom instead of compressing the final visible lines.
- In a vertical `rows(...)` container, direct `text` children must behave as fixed-height widgets at their preferred configured height, with the remaining vertical space going to flexible siblings unless one or more `vertical_spring` children consume that leftover first.
- The dedicated `drive_usage_list` section must provide a drive-usage bar thickness setting plus separate header-gap and row-gap controls so storage header spacing and data-row spacing can be tuned independently from row content height and from the thinner CPU/GPU metric bars.
- The dedicated `drive_usage_list` section must also provide one shared read/write activity-column width, the number of stacked activity segments, and the gap between those segments.
- The dedicated `drive_usage_list` section must provide separate gap controls for the activity-to-usage transition and the usage-bar-to-percent transition so storage row alignment can be tuned without changing every column spacing together.
- The dedicated widget sections must own the widget-level geometry that affects visual rhythm, including metric bar thickness, throughput plot chrome sizes, gauge ring geometry and text bottom offsets, and the fixed widths used by the storage drive row columns.
- The renderer must not rely on buried widget-spacing or widget-geometry pixel literals for text, footer, clock, gauge, or throughput sizing; those visual sizes must come from `config.ini` widget sections, with only non-visual safety clamps left in code.
- After the first layout pass resolves every gauge slot, the renderer must derive one shared fitted gauge radius from the most constrained resolved gauge bounds and use that same gauge size for every gauge render in the active layout.
- When gauge preferred height is needed before that shared fitted radius exists, it must be derived from the configured gauge text bottom offsets, ring thickness, outer padding, and measured gauge text metrics instead of a dedicated fixed-size config entry.
- Widths that are fully determined by fixed renderer text such as throughput axis labels, drive-letter labels, and the `100%` drive percent column should be measured from the configured fonts at layout load, with only the throughput axis width widened by its dedicated widget-section padding entry.
- Clock time and date preferred heights must come directly from the measured dedicated `[fonts].clock_time` and `[fonts].clock_date` metrics so small cards keep both lines visible without extra widget padding inflating the reservation.
- Text-widget preferred heights must come from the dedicated `[fonts].text` metrics plus `[text].bottom_gap`, `network_footer` preferred heights must come from the dedicated `[fonts].footer` metrics plus `[network_footer].bottom_gap`, and `vertical_spacer(widget_name)` preferred heights must match the preferred height of the referenced widget type.
- The layout language must use `vertical_spring` as an explicit flexible filler that absorbs the remaining height inside `rows(...)`, splits that height among multiple springs by weight, and lets layouts such as `rows(drive_usage_list, vertical_spring)` top-pack their content without a separate vertical-container kind.
- In a vertical `rows(...)` container, any resolved fixed-height direct child must keep its preferred configured height and any remaining height reduction must be absorbed by flexible non-spring siblings such as throughput plots when no `vertical_spring` is present.
- The shipped `vertical_spacer(network_footer)` layout pattern must reserve the same fixed preferred height as `network_footer` while drawing nothing.
- The renderer must obtain widget data through a separate metric-source abstraction that can provide text, unified resolved metrics for both `gauge(...)` and `metric_list(...)`, throughput series, and drive rows by metric name.
- Each published telemetry snapshot must advance a snapshot revision when its rendered content changes so renderer-side metric caches can be reused only across unchanged snapshots.
- Live telemetry, retained histories, and derived widget ratios must treat non-finite sampled values as unavailable or empty so resume-time provider glitches cannot propagate NaN or infinity into rendering math.
- Retained metric histories stored in the runtime snapshot and dump must keep raw sampled values in their native runtime units, while metric-list and gauge rendering normalize those retained values through the current `[metrics]` scale at draw time.
- Metric-list rows, gauge fill, and their retained recent-peak history series must use the same `[metrics]`-driven normalization scale for each bound metric so fill and peak rendering stay aligned.
- Dashboard display labels and units must come from `[metrics]` instead of hard-coded widget strings, with metric display formatting driven by the metadata-owned metric style for that metric id.
- The renderer must support a blank rendering mode that preserves panel chrome, card titles, card icons, CPU and GPU names, drive labels, and empty chart or bar tracks while omitting dynamic metric text, time, date, plot lines, chart leaders, peak ghosts, gauge fill, and drive activity or usage fill.

### Runtime actions tied to config

- The popup menu must provide `Reload Config` before `Save Config` and immediately apply reloaded `config.ini` changes to the live dashboard so UI experiments can round-trip without restarting the app.
- The popup menu must provide an `Edit layout` toggle that switches the dashboard into interactive layout-edit mode, shows that item checked while the mode is active, and lets the same menu item turn the mode back off.
- Layout-edit mode must stay active when the user enters `Move`, switches layouts, changes scale, reloads config, or changes runtime network or storage selections, and it must turn off only when the user explicitly clicks `Edit layout` again or when `Config To Display` completes successfully.
- Double-clicking the dashboard window must invoke the popup menu's default action, using `Edit ...` when layout-edit mode is active and the pointer is over an actionable guide or anchor and otherwise using `Move`, and the dashboard-window popup menu must show that same default item in bold.
- When that dashboard-window default action enters `Move`, the cursor must stay glued to the exact dashboard point that was double-clicked, while choosing `Move` from the popup menu must keep the existing centered follow behavior.
- The command line must accept `/edit-layout` to start the dashboard with layout-edit guides already enabled, and that same switch must make diagnostics screenshots render the guides.
- The command line must accept `/edit-layout:<widget-name>` for diagnostics screenshots, force the first visible widget of that type into the same outlined and widget-guide-rendering state as a live hover, and fail the export when the requested widget type is not present in the active layout.
- The command line must also accept `/edit-layout:horizonatal-sizes` and `/edit-layout:vertical-sizes` for diagnostics validation, rendering and numbering every visible horizontal size ruler or every visible vertical size ruler without requiring an active drag.
- The popup menu must provide a `Layout` submenu that lists every configured layout name, shows the active layout with a radio check, and on selection switches `display.layout`, reapplies the active named layout, and resizes the window immediately.
- The popup menu must provide a `Scale` submenu whose first radio entry is `Default`, followed by `100%`, `150%`, `200%`, `250%`, `300%`, and a trailing `Custom...` action.
- The `Scale` submenu must show `Default` checked when `display.scale` is unset, show the matching percentage checked when `display.scale` matches one of the predefined entries, and when `display.scale` is a non-default custom value it must insert that percentage into the ordered predefined list and show it checked.
- Selecting `Default` in the `Scale` submenu must clear the explicit scale override and return the dashboard to monitor-DPI-derived sizing immediately.
- Selecting a predefined percentage in the `Scale` submenu must set `display.scale` to that fractional value and immediately resize and rerender the live dashboard without restarting.
- Selecting `Custom...` in the `Scale` submenu must open a small modal percentage-entry dialog and apply the entered positive percentage as the new `display.scale` override.
- The popup menu must provide a `Network` submenu that lists every runtime network candidate with an IPv4 address, using the same `adapter name | IP address` footer text shown by the network footer widget.
- The `Network` submenu must show a radio check on the adapter selected by the runtime selection flow, even when `network.adapter_name` is empty or does not match any enumerated adapter.
- Selecting a `Network` submenu item must set `network.adapter_name` to that adapter name and apply the selection immediately without restarting the app.
- The popup menu must provide a `Storage drives` submenu that lists every active non-network fixed or removable drive with a checkbox per drive.
- Each `Storage drives` submenu item must show `drive letter | volume label | size`, omitting only the label text when the volume has no label.
- Clicking a `Storage drives` submenu item must add or remove that drive letter in `[storage] drives`, keep the saved drive list sorted by drive letter, and refresh the storage usage list immediately without restarting the app.
- The popup menu must provide a `Config To Display` submenu that lists every enumerable display by friendly name plus physical resolution, enables displays whose physical resolution matches the active layout's aspect ratio, and on selection must set `display.monitor_name`, set `display.position` to `0,0`, set `display.scale` to the fitted fractional scale that makes the active layout fill that display exactly, render a blank dashboard image to `telemetry_blank.png` beside the executable using that fitted scale, set `display.wallpaper` to `telemetry_blank.png`, save the updated `config.ini`, and immediately apply that wallpaper to the selected display.
- Turning off layout-edit mode while it has unsaved changes must show a warning dialog that offers `Save Changes`, `Keep Editing`, and `Discard Changes`, in that order, where `Save Changes` persists the current live config and then exits layout-edit mode and `Discard Changes` restores the last saved edit-session config before exiting layout-edit mode.
- Exiting the application while layout-edit mode has unsaved changes must show a warning dialog that offers `Save Changes`, `Exit Without Saving`, and `Cancel`, and that dialog must explicitly state that exiting without saving discards the unsaved changes.
- Reloading config while layout-edit mode has unsaved changes must show a warning dialog that offers `Save Changes`, `Reload Without Saving`, and `Cancel`, and reloading without saving must discard the unsaved edit-session changes while leaving layout-edit mode active after the reload.
- The config reload path must tear down the active telemetry runtime before reinitializing vendor-backed telemetry providers so AMD GPU metrics continue working after save/reload round-trips.
- When `Reload Config` reapplies saved placement onto a monitor with a different DPI scale, it must preserve the configured logical window size without double-scaling the restored physical window bounds.
- The popup menu must expose an `Auto-start on user logon` toggle that shows a check mark only when the machine-wide `HKLM\Software\Microsoft\Windows\CurrentVersion\Run\SystemTelemetry` entry points to the running executable path, removes that entry when clicked while checked, and otherwise writes the running executable path there when clicked.
- When the current process cannot update that machine-wide `Run` entry directly, the auto-start toggle must prompt for elevation and complete the change through an elevated helper instance of the same executable.
- When `network.adapter_name` is left empty, auto-selection should prefer the active adapter that best represents the routed connection, favoring adapters with a usable default gateway and IPv4 address over host-only or otherwise unrouted virtual adapters.
- When `network.adapter_name` is set to a saved adapter name such as `Ethernet`, adapter selection should prefer an exact case-insensitive alias or description match and only fall back to substring matching when no exact match exists.
- When `network.adapter_name` is set but does not match any current non-loopback, up adapter with an IPv4 address, runtime selection must fall back to the same auto-selection logic used when the setting is empty.
- The layout bindings `board.temp.<name>` and `board.fan.<name>` must be the only source of truth for which logical named board metrics are requested at runtime.
- The board provider must receive the set of requested logical board temperature and fan names by scanning all `gauge(...)`, `metric_list(...)`, and text metric references that begin with `board.temp.` or `board.fan.`.
- The `[board]` section must map each requested logical `board.temp.*` or `board.fan.*` metric name to the board-specific sensor name that the active board provider uses for lookup.
- The `Save Config` action must persist the current auto-selected network adapter name alongside the display placement without adding any separate board-sensor selection state.
- The `Save Config` action must also persist the current `[storage] drives` selection.
- The `Save Config` action must also persist any in-memory layout weight edits made through interactive layout editing.
- While layout-edit mode is active, the renderer must draw thin config-colored separator guides over every resolved `rows(...)` and `columns(...)` split, highlight the hovered non-empty widget by outlining its resolved widget box, show any supported widget-local size guides inside that hovered widget, and update the cursor to the matching horizontal or vertical resize shape, or a crosshair for angular gauge editing, when the pointer hovers a draggable guide.
- While layout-edit mode is active, hovering a container split guide must also draw a dotted outline around the immediate resolved `rows(...)` or `columns(...)` container rect that owns that guide, using the same dotted-outline visual style used for text-target edit highlights.
- While layout-edit mode is active, hovering the top band of a card through the header and header-content-gap area, ending at the content top edge, must activate that card's shared `[card_style]` chrome guides and anchors.
- While layout-edit mode is active, hovering a rendered widget text run or card title text run must also draw a light dotted highlight tightly around that run's measured bounds plus a small anchor at the run's upper-right corner, with dynamic text using the current draw-time bounds and static text using the cached layout-time measured bounds, and dragging that anchor left or right must live-update the shared configured font-size entry used by that run.
- While layout-edit mode is active, hovering any rendered metric-backed text run whose label or rendered value comes from `[metrics]` must also show one wedge-shaped anchor at the text highlight's upper-left corner, formed by the top and left sides of a small square corner that points into the text rect.
- While layout-edit mode is active, hovering a card title text run must also show one wedge-shaped anchor at the text highlight's upper-left corner, using the same maintained wedge geometry and hit behavior as other text-label wedges, and that anchor must target the edited card's `[card.<id>].title` value.
- While layout-edit mode is active, hovering a metric-list fill bar or drive-usage fill bar must also draw a light dotted highlight around that bar plus a small anchor centered on the bar's bottom edge, and dragging that anchor up or down must live-update the corresponding shared configured bar-height entry.
- While layout-edit mode is active, hovering a `gauge` widget must also draw a small diamond-shaped anchor centered on the gauge's top edge, and dragging that anchor in either axis must live-update `[gauge].segment_count`.
- While layout-edit mode is active, the `gauge` widget must also expose concentric circular size anchors centered on the gauge, with the outer circle live-updating `[gauge].outer_padding` and the inner circle live-updating `[gauge].ring_thickness`.
- While layout-edit mode is active, hovering a `gauge` widget anywhere inside its bounds must keep the `segment_count`, `outer_padding`, and `ring_thickness` anchors visible even when the pointer is currently over one of the gauge's other guides or anchor handles.
- Widget-local actionable hit-testing must use one shared maintained priority order that is visible in the grouped layout-edit parameter enum, with small actionable handles first, shared dashboard and card gap-anchor handles including the dashboard `outer_margin` anchor before overlapping widget-local horizontal and vertical guides, large circular gauge ring handles after those, and container split guides always last.
- While layout-edit mode is active, hovering any actionable widget-local guide, actionable gap-anchor handle, actionable edit-anchor handle, low-priority color-edit region, or container split guide must also show a standard Win32 tooltip that can extend beyond the dashboard window; widget-local guides, gap anchors, and anchor handles must keep the first line formatted as `[section] parameter = current_value`, except that shown `[fonts]` anchor-handle tooltips must use the full `face,size,weight` font spec exactly in the config-file shape, shown `[colors]` tooltips must use `#RRGGBB`, and container split guides must format the first line as `[section] layout = ... rows(left_child:left_weight, right_child:right_weight)` or `[section] layout = ... columns(left_child:left_weight, right_child:right_weight)` for card layouts and `[layout.<name>] cards = ... rows(...)` or `columns(...)` for dashboard-card layouts using the two neighboring children around the hovered separator; the second line must show a localized description from the embedded shared key=value localization catalog, using extensible `config.section.parameter` keys for config-parameter edits and one shared maintained key for container split guides; the tooltip must follow the same hit-testing priority and same concrete edit target that the cursor and mouse-down logic use, it must never describe a different edit than the one that would start from the current pointer position, and low-priority color-edit regions must yield to overlapping actionable guides, handles, and anchors.
- Metric-text wedge anchors must use tooltip first lines formatted as `[metrics] metric_id = scale,unit,label` with the same config-file value shape used by `[metrics]`, and their second line must use one shared localized metric-definition description.
- Card-title wedge anchors must use tooltip first lines formatted as `[card.<id>] title = value`, and their second line must use the shared localized `config.card.title` description.
- While layout-edit mode is active, right-clicking while the pointer is over the same actionable widget-local guide, actionable gap-anchor handle, actionable edit-anchor handle, low-priority color-edit region, or container split guide must prepend one `Edit <item> ...` action to the popup menu for that exact hovered target, and choosing that action must open one unified fixed-size modeless configuration editor with a left-side parameter tree and a right-side live editor pane focused on the matching config item or neighboring layout-weight pair. Entering layout-edit mode by itself must keep only the dashboard visible until the user explicitly opens that editor. The editor window and dashboard window must stay as separate top-level windows so clicking either one can raise it above the other, opening the editor from the dashboard must immediately raise it above the dashboard, turning off layout-edit mode must close the editor when it is open, and closing the editor window must drive the same custom unsaved-session prompt used when turning off layout-edit mode, with explicit `Save`, `Discard`, and `Cancel` buttons instead of explained `Yes` or `No` buttons. The dialog must provide a filter box above the tree that matches node labels and config-location text case-insensitively, preserves ancestor nodes for matches, auto-expands the filtered branches, and restores the original config-template ordering when the filter is cleared. The tree must follow the embedded `resources/config.ini` section and key order for ordinary editable config fields, include the whole editable `[colors]` section even when some colors are not reachable through hover targets, show standard expanded and collapsed indicators for nodes with children, show only the active `[layout.<name>]` section, show only reachable `[card.<id>]` sections, and build the `cards` or `layout` subtree from the current nested container structure using comma-separated neighboring-child labels for editable weight pairs. Numeric and text fields must edit in one full-width value box with a short format hint, font fields must edit face, size, and weight together with a dropdown list of installed font families plus a fitted one-line sample that uses a representative text for the selected font target, color fields must edit through an editable `#RRGGBB` field, a color swatch, a sample label, separate `R`, `G`, and `B` integer fields with paired 0-255 sliders, and a secondary `Pick...` color-picker button, neighboring layout weights must edit in two integer fields with their directional labels, and metric fields must show display style read-only ahead of the editable metric rows. The right-hand pane must be laid out programmatically from measured control sizes and wrapped text heights instead of fixed resource-script row positions, using one maintained vertical layout pass that positions the header, a content-fitted editor group, status row, and footer hint from the current dialog content. The editor group must keep consistent visible inner border padding on every side, including the top framed edge, and the footer hint must bottom-align with the tree's bottom edge on the left while keeping the same outer edge margins used by the rest of the dialog. The bold header title must align to the top edge shared by the filter edit and the vertical separator, the horizontal gap from the separator to the right-hand pane must match the separator-to-tree gap on the left, and the header description must always reserve at least two lines of height so tree navigation does not shift the editor group when descriptions alternate between one and two wrapped lines. Metric read-only rows, text-entry rows, and combo rows must share one aligned row height based on the visible field height so the `Style`, editable metric fields, and optional `Binding` row line up consistently. The dialog row labels and action buttons must stay visually aligned with their associated controls, hidden editor families must not reserve vertical space, wrapped secondary description, summary, hint, status, or footer text must remain fully visible without clipping or overlapping any visible control, and tree-selection changes must update the right-hand pane atomically so description reflow between one-line and two-line headers does not expose intermediate control positions. The right-hand pane must show a header title, the selected tree node's config location line in secondary text, a localized description for every tree node including section, group, and container nodes, and a structural-node summary when the selected node does not expose a leaf editor. The status row must show inline validation or preview status without discarding the last valid live preview, expose a `Revert Field` action that restores only the selected item from the edit-session saved config snapshot, and keep a persistent footer hint that closing the window or turning off `Edit layout` saves or discards the whole edit session. While this modeless editor is open, dashboard hover must not change the selected tree item, starting a dashboard drag must select the dragged item in the tree without raising the editor, active drags must defer right-hand editor retargeting until the drag ends, double-clicking a dashboard edit target must select the matching tree item and raise the editor window, and the dashboard must stay mouse-transparent for screen regions currently covered by the editor while the editor is above it in z order, including suppressing dashboard tooltip and cursor-shape updates for those covered pointer positions. When the dashboard is above the editor in z order, the dashboard must stop drawing the selected tree-node highlight and fall back to ordinary hovered-item layout-edit highlighting until the editor is brought above it again. The `layout.<name> -> cards` node must carry the same highlight behavior as its parent layout section, the `card.<id> -> layout` node must carry the same highlight behavior as its parent card section, selecting a tree leaf must highlight every matching on-screen guide, anchor, or color-edit region that edits that item without switching the corresponding widgets into the hovered-widget outline state, selecting a widget-section root must highlight every rendered widget of that type, selecting a visible container node must highlight that specific nested container, selecting `[layout.<name>]` must highlight the whole dashboard window bounds with the dotted outline drawn inside the window edge so it stays visible, selecting `[card.<id>]` must highlight every rendered instance of that card including embedded card references, selecting `card_style` must highlight all cards, selecting `fonts` must highlight all editable text targets and `text` widgets, selecting `dashboard` must highlight the dashboard outer edge, and all valid edits must preview live into the dashboard for the whole edit-layout session.
- While layout-edit mode is active, right-clicking while the pointer is over the same actionable widget-local guide, actionable gap-anchor handle, actionable edit-anchor handle, low-priority color-edit region, or container split guide must prepend one `Edit <item> ...` action to the popup menu for that exact hovered target, and choosing that action must open one unified fixed-size modeless configuration editor with a left-side parameter tree and a right-side live editor pane focused on the matching config item or neighboring layout-weight pair. Entering layout-edit mode by itself must keep only the dashboard visible until the user explicitly opens that editor. The editor window and dashboard window must stay as separate top-level windows so clicking either one can raise it above the other, opening the editor from the dashboard must immediately raise it above the dashboard, turning off layout-edit mode must close the editor when it is open, and closing the editor window must drive the same custom unsaved-session prompt used when turning off layout-edit mode, with explicit `Save`, `Discard`, and `Cancel` buttons instead of explained `Yes` or `No` buttons. Discarding that edit session must restore only the saved `LayoutConfig` state, while keeping the current display placement, selected named layout, display scale, preferred network adapter, and selected storage drives unchanged. The dialog must provide a filter box above the tree that matches node labels and config-location text case-insensitively, preserves ancestor nodes for matches, auto-expands the filtered branches, and restores the original config-template ordering when the filter is cleared. The tree must follow the embedded `resources/config.ini` section and key order for ordinary editable config fields, include the whole editable `[colors]` section even when some colors are not reachable through hover targets, show standard expanded and collapsed indicators for nodes with children, show only the active `[layout.<name>]` section, show only reachable `[card.<id>]` sections, and build the `cards` or `layout` subtree from the current nested container structure using comma-separated neighboring-child labels for editable weight pairs. Numeric and text fields must edit in one full-width value box with a short format hint, font fields must edit face, size, and weight together with a dropdown list of installed font families plus a fitted one-line sample that uses a representative text for the selected font target, color fields must edit through an editable `#RRGGBB` field, a color swatch, a sample label, separate `R`, `G`, and `B` integer fields with paired 0-255 sliders, and a secondary `Pick...` color-picker button, neighboring layout weights must edit in two integer fields with their directional labels, and metric fields must show display style read-only ahead of the editable metric rows. The right-hand pane must be laid out programmatically from measured control sizes and wrapped text heights instead of fixed resource-script row positions, using one maintained vertical layout pass that positions the header, a content-fitted editor group, status row, and footer hint from the current dialog content. The editor group must keep consistent visible inner border padding on every side, including the top framed edge, and the footer hint must bottom-align with the tree's bottom edge on the left while keeping the same outer edge margins used by the rest of the dialog. The bold header title must align to the top edge shared by the filter edit and the vertical separator, the horizontal gap from the separator to the right-hand pane must match the separator-to-tree gap on the left, and the header description must always reserve at least two lines of height so tree navigation does not shift the editor group when descriptions alternate between one and two wrapped lines. Metric read-only rows, text-entry rows, and combo rows must share one aligned row height based on the visible field height so the `Style`, editable metric fields, and optional `Binding` row line up consistently. The dialog row labels and action buttons must stay visually aligned with their associated controls, hidden editor families must not reserve vertical space, wrapped secondary description, summary, hint, status, or footer text must remain fully visible without clipping or overlapping any visible control, and tree-selection changes must update the right-hand pane atomically so description reflow between one-line and two-line headers does not expose intermediate control positions. The right-hand pane must show a header title, the selected tree node's config location line in secondary text, a localized description for every tree node including section, group, and container nodes, and a structural-node summary when the selected node does not expose a leaf editor. The status row must show inline validation or preview status without discarding the last valid live preview, expose a `Revert Field` action that restores only the selected item from the edit-session saved config snapshot, and keep a persistent footer hint that closing the window or turning off `Edit layout` saves or discards the whole edit session. While this modeless editor is open, dashboard hover must not change the selected tree item, starting a dashboard drag must select the dragged item in the tree without raising the editor, active drags must defer right-hand editor retargeting until the drag ends, double-clicking a dashboard edit target must select the matching tree item and raise the editor window, and the dashboard must stay mouse-transparent for screen regions currently covered by the editor while the editor is above it in z order, including suppressing dashboard tooltip and cursor-shape updates for those covered pointer positions. When the dashboard is above the editor in z order, the dashboard must stop drawing the selected tree-node highlight and fall back to ordinary hovered-item layout-edit highlighting until the editor is brought above it again. The `layout.<name> -> cards` node must carry the same highlight behavior as its parent layout section, the `card.<id> -> layout` node must carry the same highlight behavior as its parent card section, selecting a tree leaf must highlight every matching on-screen guide, anchor, or color-edit region that edits that item without switching the corresponding widgets into the hovered-widget outline state, selecting a widget-section root must highlight every rendered widget of that type, selecting a visible container node must highlight that specific nested container, selecting `[layout.<name>]` must highlight the whole dashboard window bounds with the dotted outline drawn inside the window edge so it stays visible, selecting `[card.<id>]` must highlight every rendered instance of that card including embedded card references, selecting `card_style` must highlight all cards, selecting `fonts` must highlight all editable text targets and `text` widgets, selecting `dashboard` must highlight the dashboard outer edge, and all valid edits must preview live into the dashboard for the whole edit-layout session.
- Each `card.<id>` root whose card id appears in the top-level dashboard `cards` layout must include one top-level `title` leaf ahead of the `layout` subtree, while reachable embedded-only card sections must omit that `title` leaf, and selecting a shown title leaf must highlight every matching card-title wedge anchor on screen without turning on hovered-widget outlines.
- The unified layout-edit tree must include the editable `[metrics]` section in embedded-template order, and selecting a metric leaf must focus every matching metric-text wedge anchor on screen without turning on hovered-widget outlines.
- Metric leaves in the unified layout-edit editor must show display style read-only from the built-in metric metadata, label always editable, unit editable except for `label_only`, and scale editable only when the metric does not use telemetry-provided `*` scale and the style is not `label_only`.
- Metric leaves whose id begins with `board.temp.` or `board.fan.` must also show a `Binding` dropdown that edits the corresponding `[board]` sensor-name mapping for that logical metric name, lists the board provider's currently available temperature or fan sensor names, and live-updates the rendered board metric preview in the open editor session.
- While that popup menu or any transient modal prompt opened from layout-edit mode is open, layout-edit hover hit-testing, tooltip refresh, cursor-shape overrides, and any layout-edit press or drag handling must stay suspended, and any already-active layout-edit interaction must cancel immediately, so the popup or prompt keeps the normal topmost input behavior instead of reflecting or continuing dashboard edits underneath.
- Diamond-shaped segment-count anchors must use a tight hit region that stays at the rendered diamond bounds instead of inheriting the larger padded hit area used by circular anchors.
- While any layout-edit guide or edit anchor drag is active, the renderer must switch only that actively dragged guide or anchor highlight to the configured `[colors].active_edit_color` and draw its visible guide or outline with a heavier stroke than the idle highlight.
- While the unified layout-edit dialog is open, the currently selected tree item must reuse that same active-edit color and heavier visible stroke for every matching on-screen guide or anchor, including every matching widget-local copy of a shared parameter, while leaving whole-widget hover outlines off.
- While a container split guide drag is active, and while the unified layout-edit dialog focuses a neighboring layout-weight tree leaf, that same owning container dotted outline must stay visible and use the active-edit color plus the heavier stroke.
- The shared `[card_style]` section must expose `card_padding`, `card_radius`, `card_border`, `header_icon_size`, `header_icon_gap`, `header_content_gap`, `row_gap`, and `column_gap` as the maintained card chrome and in-card spacing keys.
- Card headers must size themselves from the larger of the measured title text height and `[card_style].header_icon_size`, without any separate `[card_style].header_height` override, and `[card_style].header_content_gap` must apply only when the card renders a title or icon header.
- Card layout editing must expose one horizontal guide for `[card_style].card_padding` at the top inner padding edge, one square horizontal-drag anchor for `[card_style].card_radius` at the top-left rounded-corner tangent point, one circular radial size anchor for `[card_style].card_border` centered on the top border, one vertical guide for `[card_style].header_icon_gap` at the title start when the card renders both an icon and title, and one horizontal guide for `[card_style].header_content_gap` at the content top when the card renders a header.
- Hovering a card header icon in layout-edit mode must draw a dotted highlight around the icon plus a square upper-right size anchor that live-updates `[card_style].header_icon_size`.
- Layout-edit mode must expose at most one maintained `row_gap` gap anchor and one maintained `column_gap` gap anchor for the shared dashboard cards tree, placed on the first encountered rendered `rows(...)` or `columns(...)` container in dashboard traversal order, and at most one maintained `row_gap` gap anchor and one maintained `column_gap` gap anchor per rendered card, placed on the first encountered rendered `rows(...)` or `columns(...)` container in that card's traversal order, using `[dashboard].row_gap` or `[dashboard].column_gap` for dashboard containers and `[card_style].row_gap` or `[card_style].column_gap` for card containers.
- Layout-edit mode must expose one maintained horizontal dashboard `outer_margin` gap anchor in the top-left window corner, spanning from the window left edge to the dashboard content left edge at the dashboard content top edge, so dragging its right-end handle updates `[dashboard].outer_margin`.
- Each gap anchor must render as a line spanning the first rendered gap with short T-caps at both ends plus one small square drag handle at the bottom end for `row_gap` or the right end for `column_gap`, and dragging that handle down or right must increase the stored gap.
- The three shared dashboard spacing anchors for `outer_margin`, `row_gap`, and `column_gap` must stay visible for the whole layout-edit session, and card gap anchors must stay visible only while the pointer is inside the corresponding rendered card.
- While layout-edit mode is active, the `throughput` widget must also expose circular size anchors for `[throughput].leader_diameter`, `[throughput].plot_stroke_width`, and `[throughput].guide_stroke_width`; each anchor circle must render slightly larger than the size it edits and dragging that circle in any direction must live-update the stored value from the pointer distance to the anchor center.
- The full `[colors]` section must be editable through the unified configuration tree in the same embedded-template order as `resources/config.ini`.
- While layout-edit mode is active, hovering a text area away from its font-size anchor handle must switch the cursor to a picker-like low-priority color-edit crosshair, show the matching color tooltip offset far enough from the pointer to stay readable with large cursor themes, and target the text color for the default `Edit ...` action without starting a drag.
- The shared `[colors].icon_color` entry must control the color of card header icons, and double-clicking a card header icon in layout-edit mode must target that color entry through the same low-priority color-edit flow used by other color regions.
- While layout-edit mode is active, pill bars must expose low-priority color-edit regions that split the pill into a left `[colors].accent_color` half and a right `[colors].track_color` half, gauge ring areas must expose the same left and right split for fill and track colors, and throughput graph areas must expose one low-priority `[colors].accent_color` region.
- Card header icons must continue using the embedded white PNG resources, and the renderer must recolor those white icon pixels at runtime to the configured `[colors].icon_color` instead of requiring separate per-color assets.
- Throughput circular size anchors must render as standalone circles without a dotted projected target rectangle, and their interactive region must stay at the circle outline plus the same slightly padded distance tolerance used by other circular anchors.
- Circular anchor handle hit-testing must follow the distance from the pointer to the rendered circle outline, using a small padded tolerance around that outline instead of a rectangular handle hit box.
- The highest-priority actionable-handle group must include the gauge and drive-usage diamond anchors, the small upper-right font-size anchors on hovered text and card titles, the card-style `card_radius`, `card_border`, and `header_icon_size` anchors, the metric-list and drive-usage bar-height anchors, and the throughput circular handles for `leader_diameter`, `plot_stroke_width`, and `guide_stroke_width`, so those handles win even when they overlap larger editable circles or guide regions.
- Horizontal guides in `rows(...)` must not be drawn next to direct fixed-height children or `vertical_spring` children whose size is not editable through row weights.
- Dragging a layout-edit guide must adjust the two adjacent child weights live, immediately relayout and repaint the dashboard, and reseed the dragged container's editable integer weights from the current resolved child extents when the drag begins, excluding any direct fixed-height row children and any `vertical_spring` children from that reseeded total.
- While a container layout-edit guide drag is active, the renderer must keep using the same full-fidelity widget draw path used by ordinary repaint frames instead of switching to a reduced drag-preview renderer.
- The `metric_list` widget must expose widget-local edit guides for `[metric_list].label_width` and `[metric_list].row_gap` while the widget is hovered in layout-edit mode, with the vertical guide at the label/value split and horizontal guides after each visible non-empty row so dragging either guide updates the shared metric-list chrome live without extending into empty missing-row space.
- The `text` widget must expose one widget-local horizontal edit guide for `[text].bottom_gap` while the widget is hovered in layout-edit mode, drawn at the bottom edge of the widget so dragging that guide down increases and dragging it up decreases the fixed extra space reserved below the text line.
- The `network_footer` widget must expose one widget-local horizontal edit guide for `[network_footer].bottom_gap` while the widget is hovered in layout-edit mode, drawn immediately below the footer text line and above the reserved gap so dragging that guide up increases and dragging it down decreases the visible space below the footer text.
- The `metric_list` widget must treat `[metric_list].label_width` as the full left-side reservation up to the start of the value and bar region, with no separate metric-list gutter key.
- The `throughput` widget must expose widget-local edit guides for `[throughput].axis_padding` and `[throughput].header_gap` while the widget is hovered in layout-edit mode, with the vertical guide drawn at the plot-left boundary between the vertical scale gutter and the plotted area and the horizontal guide drawn at the header-to-graph boundary so dragging either guide relayouts that shared spacing live.
- The `throughput` widget must place its `leader_diameter` size anchor at the middle of the plot's right edge, its `plot_stroke_width` size anchor at the middle of the plot's left edge, and its `guide_stroke_width` size anchor on a centered horizontal plot guide whose center stays fixed while the edited stroke width changes so the handle stays visually stable.
- The `drive_usage_list` widget must expose widget-local vertical edit guides for `[drive_usage_list].label_gap` at the left edge of the `R` activity column, `[drive_usage_list].rw_gap` at the left edge of the `W` activity column, `[drive_usage_list].bar_gap` at the left edge of the usage bar, `[drive_usage_list].percent_gap` at the right edge of the usage bar, `[drive_usage_list].activity_width` at the right edge of the `W` activity column, and `[drive_usage_list].free_width` at the free-space column, together with horizontal guides for `[drive_usage_list].activity_segment_gap` spanning both activity columns at the top edge of the lowermost segment, `[drive_usage_list].header_gap` below the header, and `[drive_usage_list].row_gap` after each visible non-empty row so dragging those guides updates the shared spacing live without extending into empty missing-row space, and it must also expose one diamond anchor centered on the top edge of the read/write indicator band so dragging that anchor in either axis live-updates `[drive_usage_list].activity_segments`.
- While layout-edit mode is active, hovering a `drive_usage_list` widget anywhere inside its bounds must keep the `activity_segments` diamond anchor visible even when the pointer is currently over one of the drive-usage guides or anchor handles.
- The `drive_usage_list` activity-segment gap must clamp to the largest value that still leaves every stacked read/write segment at least one pixel tall, so dragging that guide cannot move past the last valid segment layout or produce inverted or negative-height visuals.
- The `gauge` widget must expose widget-local edit guides for `[gauge].sweep_degrees`, `[gauge].segment_gap_degrees`, `[gauge].value_bottom`, and `[gauge].label_bottom` while the widget is hovered in layout-edit mode, with short diagonal radial guides drawn across the ring at the current right-end arc boundary and at the end of the first segment so dragging the sweep guide edits only the gauge end side and dragging the first-segment guide edits the apparent first-segment width while converting that movement back into the stored segment gap, with horizontal guides at the value and label text bottoms so dragging those guides updates the text placements live, and it must also expose the top-edge diamond anchor that drags `[gauge].segment_count` in either axis plus concentric circle anchors for `[gauge].outer_padding` and `[gauge].ring_thickness`.
- Widget-guide edits whose valid range depends on other live config values must clamp in the layout-edit controller against the current config instead of in the config schema, so direct drags stay responsive while loaded configs and non-interactive edits keep only field-local safety clamps.
- Full-font layout edits must commit through the shared config-schema clamp path so both font `size` and font `weight` remain positive during live preview, OK/apply, and other non-drag font writes.
- While a layout-edit guide drag is active, the renderer must compare each affected descendant widget against every other widget of the same type in the active layout and draw paired measurement rulers on widgets whose dragged-axis size differs by no more than the configured threshold, while skipping `vertical_spring` widgets and any widgets whose row height is fixed.
- Once a dragged widget falls within the configured same-type size threshold that makes the ruler visible, the drag must snap to the nearest exact same-size group chosen from the drag-start layout, solving the snapped guide weights through iterative relayout passes so nested weighted hierarchies can converge on the matching extent; when multiple groups are equally close at drag start, the first group in layout order wins.
- When multiple same-type widgets in one vertical or horizontal stack share the same dragged-axis extent by construction, the measurement ruler for that type and extent must draw only on the first widget in that stack, both for the widgets being edited and for matching widgets elsewhere in the active layout.
- When same-type widgets match exactly on the dragged axis, the measurement ruler must show centered notch markers, with the notch count coming from that exact-match type's active drag ordinal where a type is the widget type plus the matched dragged-axis extent.
- Holding `Alt` during a layout-edit drag must temporarily disable same-size snapping while keeping the free-drag weight adjustment active.
- Pressing `Esc` must exit move mode, and while layout-edit mode is active it must only cancel any active layout-edit interaction without turning layout-edit mode off.

Diagnostics requirements live in `docs/diagnostics.md`.

## Telemetry sources

### CPU telemetry

CPU telemetry must provide:

- CPU model name
- CPU load
- CPU clock
- RAM usage
- Gigabyte motherboard named temperature and fan metrics on the target Gigabyte X570 AORUS system through the installed Gigabyte SIV hardware-monitor stack, without LibreHardwareMonitor or OpenHardwareMonitor

Additional rule:

- CPU package power is not required.

### GPU telemetry

GPU telemetry must provide:

- GPU load
- GPU dedicated VRAM usage
- GPU total dedicated VRAM capacity
- AMD GPU temperature, clock, and fan speed from AMD's ADLX API

### Provider behavior

- If AMD ADLX is unavailable or unsupported, the dashboard should continue running and leave AMD vendor metrics unavailable.
- Storage throughput should come from system-wide disk I/O counters, not only from the subset of configured drive letters shown in the storage usage list.
- Per-drive storage activity indicators should come from per-drive logical-disk I/O counters for the configured `[storage] drives` letters.
- Gigabyte motherboard board-metric telemetry should keep working when the Gigabyte board-specific provider is unavailable by leaving the requested `board.temp.*` and `board.fan.*` metrics unavailable.
- The Gigabyte motherboard telemetry path should identify Gigabyte boards, discover the installed SIV location from the Windows registry, load the required Gigabyte SIV .NET assemblies in-process from native C++ code, initialize the vendor hardware-monitor module against the `HwRegister` source through reflection, collect the available fan RPM and temperature readings directly from those loaded assemblies, and match requested logical `board.temp.*` and `board.fan.*` names through the configured `[board]` sensor-name mapping.

## Size and placement

- Panel size must come from the configured `layout.window` value.
- Place it as a top-level window at the monitor that is specified in configuration.
- Render the live dashboard at the native device-pixel size implied by the current monitor DPI.
- When the window moves onto a monitor with a different DPI, or when Windows reassigns it because monitors are connected or disconnected, the dashboard must recompute its device-pixel size and rerender at the new monitor scale automatically.
- Make sure the monitor to show the panel is selected by its name, not number, so it survives plugging and unplugging other monitors into the system.
- Support interactive repositioning so the user can discover and copy the monitor name plus the relative X/Y placement for configuration.

## Window controls

Add a popup menu on right-click with these actions:

- Move
- Bring On Top
- Reload Config
- Save Config
- Layout
- Network
- Storage drives
- Config To Display
- Auto-start on user logon
- Diagnostics
- Exit

The `Diagnostics` submenu must provide:

- Save full config to...
- Save dump to...
- Save screenshot to...

When Move is active, the dashboard window should follow the mouse cursor until the user places it.
While moving, show an overlay in the top-left corner with:

- Monitor friendly name
- Current monitor scale
- Relative X/Y position on that monitor

- The move overlay should size and space itself from the actual UI font metrics and monitor scale.
- The move overlay must render as part of the same frame as the dashboard content so it stays consistently on top during move-mode repaints.
- The overlay content should be easy to copy into configuration.
- The application must also read the saved relative X/Y coordinates from configuration at startup and place the window accordingly.
- The application must provide a `Save Config` action that writes the display identifier and relative X/Y placement back to the config file while preserving all other settings and unchanged explicit overrides.
- The `Save full config to...` action must open a standard Windows Save dialog, default to the current working directory, default the file name to `telemetry_full_config.ini`, and export the full embedded-template-shaped config with current live values.
- The `Save dump to...` action must open a standard Windows Save dialog, default to the current working directory, default the file name to `telemetry_dump.txt`, and write the same text dump format used by diagnostics output.
- The dump format contains only the snapshot fields that `/fake` loads and the dashboard renders, keeps retained histories in raw sampled units, and keeps the internal scalar-unit tokens used by the snapshot model for round-tripping; provider-debug details remain trace-only diagnostics data.
- The `Save screenshot to...` action must open a standard Windows Save dialog, default to the current working directory, default the file name to `telemetry_screenshot.png`, and write the same PNG output format used by diagnostics output.

## Tray behavior

- Create a system tray icon with the same popup menu as the dashboard window.
- The tray menu must expose the same actions:

- Move
- Bring On Top
- Reload Config
- Save Config
- Layout
- Network
- Storage drives
- Config To Display
- Auto-start on user logon
- Diagnostics
- Exit

- The dashboard should use normal window Z-order behavior so other windows may cover it. `Bring On Top` should raise the dashboard when it needs to be found.
- Double-clicking the tray icon should perform the same `Bring On Top` action.
- The tray popup menu must show `Bring On Top` in bold as its default action, even though the tray menu still exposes the same action list as the dashboard window.

## Single-instance behavior

- At most one dashboard instance may be running at a time.
- Starting a new copy must close the already running copy, then continue startup as the remaining instance.

## Style summary

### Visual theme

- Background: pure black (`#000000`)
- Primary text: white (`#FFFFFF`)
- Accent color: cyan/blue (`#00BFFF` or similar)

### Panels

- Rounded rectangles with thin white borders
- Consistent padding inside all panels
- Subtle spacing between panels (`8-12 px`)

### Typography

- Primary numbers (load, time): large, bold
- Labels: small, minimal (`Temp`, `Clock`, `RAM`, and similar)
- Units: slightly dimmer than values

### Visual elements

- Circular gauges for CPU/GPU load with a segmented ring drawn in the track color and only the load-sized leading segments overlaid in the shared usage color
- CPU and GPU gauges within one resolved layout must render at the same fitted size, even when their surrounding cards have different available heights.
- Horizontal usage bars with rounded leading and trailing ends, with the straight middle section shrinking naturally to zero so empty values collapse to a circle the height of the bar
- Mini graphs for network and storage throughput activity
- Clean, minimal headers
- CPU and GPU load gauges must use the same fill color for their used arc as storage and other occupancy/usage bars use for their filled portion.
- CPU and GPU load gauges must render as a segmented ring that follows the `[gauge]` config geometry, with the shipped default using a 262 degree clockwise sweep split into 33 segments with 32 shared inter-segment gaps, a bottom-centered opening derived from that total sweep, and no trailing gap after the final segment, filling clockwise from the lower-left toward the lower-right.
- Gauges must render the bound metric's configured label and unified formatted metric text instead of hard-coded load-only text, while still using the same segmented ring geometry and fill rules.
- Gauge and drive-usage segment layout resolution must keep every configured segment drawable even if a saved or indirectly derived gap would otherwise consume all remaining extent, reducing the visible segment to a hairline at worst instead of dropping it entirely.
- Gauge fill must quantize to whole pills only; any usage above 0 percent lights the first pill, additional pills round up from the usage percentage, and partially filled pills must not be drawn.
- CPU and GPU load gauges must also overlay a small translucent max-ghost on the single segmented pill that corresponds to the highest retained load ratio seen in the shared recent 30 second history window.
- Gauge and metric-row max ghosts must immediately reflect `[metrics]` scale edits without requiring telemetry to rebuild retained history.
- Each top-level panel header must show a small monochrome icon derived from the dashboard sketch for CPU, GPU, Network, Storage, and Time, with those icon assets stored under `resources\` and compiled into the executable.
- The application must define a custom app icon asset under `resources\`, embed it into the executable, and use that embedded icon for the executable and the tray icon.
- The CPU metrics stack must use the same vertical spacing and bar rhythm as the GPU metrics stack so both top cards read as a matched pair.
- The bottom row should keep enough width in the time card to avoid clipping the clock text, while giving most reclaimed horizontal space to the storage card and using slightly narrower network and storage throughput plot areas so the storage drive-usage section has more room.

### Design principles

- High contrast for readability on small screen
- Prioritize large, glanceable values
- Avoid clutter
- Consistent alignment across panels

## CPU panel (top left)

### Header

- CPU model name

### Main indicator
- Large circular gauge
- CPU load (`%`)

### Metrics

- RAM usage
- Format: used / total
- Temp (`°C`)
- Clock (`GHz`)
- Fan (`RPM`)
- System Fan (`RPM`)
### Metric-row rendering

- Each CPU and GPU metric row bar must render with rounded leading and trailing ends, with the straight middle section proportional to the current value so the bar naturally collapses to a circle at zero, and overlay a small translucent vertical capsule marker at the highest bar ratio seen in the retained recent metric history.
- Metric-row peak ghosts must use the same shared retained-history-series path, 0.5 second update cadence, and 60-sample depth as the network and storage throughput plots, for a 30 second recent-max window.

## GPU panel (top right)

### Header

- GPU model name

### Main indicator
- Large circular gauge
- GPU load (`%`)

### Metrics

- VRAM usage
- Format: used / total
- Temp (`°C`)
- Clock (`MHz`)
- Fan (`RPM`)

## Network panel (bottom left)

### Header

- Network

### Primary values
- Upload speed shown as `Up 0.3 MB/s`
- Download speed shown as `Down 24.1 MB/s`

### Graphs
- Two small scrolling graphs
- Upload history
- Download history

### Footer and graph behavior
- Show upload speed, then the upload graph directly below it, then download speed, then the download graph directly below it.
- Use smaller text for the Up/Down speed readouts than the main metric text used elsewhere.
- Draw the top-left throughput labels and the network footer with the same darker label color used by drive-list headers and other secondary labels.
- Throughput header labels must use their actual rendered text width when reserving the left side of the value row instead of relying on a cached width from hard-coded label names.
- Each graph must include a vertical axis and a small muted label showing the current max value used to scale that plot.
- Network and storage throughput plot scaling must be derived from the maximum value present in the retained history shown by that panel.
- The throughput plots should render a simple moving average over the most recent two readings so the line reflects a 1-second smoothing window, and the numeric throughput readout should show the same latest averaged point that the leader circle marks, yielding a per-last-second value that still refreshes every 0.5 seconds.
- Throughput plot scaling must use a minimum max of 10 MB/s, round the averaged max up in 5 MB/s steps through 100 MB/s, and then round it up in 50 MB/s steps above 100 MB/s.
- Throughput preferred height must come from one measured small-font header row, the configured `[throughput].header_gap`, and one measured small-font plot max-label band, rather than from a dedicated graph-height config entry.
- Throughput plots must reserve the configured `[throughput] plot_stroke_width` as top inset in the plotted area so a sample equal to the computed graph max still renders fully inside the chart bounds.
- Throughput `guide_stroke_width` must control the thickness used by the plot axes, horizontal throughput guide lines, and vertical time-marker lines.
- Network plots should draw thin horizontal guide lines every 5 MB/s.
- Storage throughput plots should use the same averaged-history scaling, draw horizontal guide lines every 5 MB/s while the averaged max stays at or below 50 MB/s, and switch those guide lines to every 50 MB/s once the averaged max exceeds 50 MB/s.
- All throughput plots should also draw synchronized vertical time markers every 10 seconds, with marker placement driven from one shared snapshot-time phase so the markers line up and scroll in sync across network and storage.
- Throughput-plot horizontal and vertical markers should render with a dedicated darker config-driven marker color so they remain visually distinct from the throughput line itself.
- All throughput plots should render a small accent-colored leader circle centered on the right-most live point, with its diameter coming from the `[throughput]` widget config and the plotted line ending at that circle center so the live value reads clearly without labels on every point.
- Show adapter name and IP address together on the same final footer line when available.

## Storage panel (bottom center)

### Structure

- Stack read and write throughput sections on the left, with the same vertical rhythm and plot behavior used by the network panel, but at a slightly narrower width so the drive-usage list has more room.
- List the drives configured in `[storage] drives` vertically on the right.

### Throughput

- Read throughput
- Write throughput
- These throughput values should come from total system disk I/O counters.

### Per-drive row

- Drive letter
- Read activity indicator
- Write activity indicator
- Usage bar
- Usage `%`
- Free space

### Header row

- No header over the drive-letter column
- `R`
- `W`
- `Usage` spanning the usage-bar and usage-percent columns
- `Free`

### Example format

- `C: [R] [W] 32% 2.5 TB`
- `D: [R] [W] 44% 534 GB`
- `E: [R] [W] 32% 5.0 TB`

### Visuals

- Two narrow vertical UV-style stacked-segment indicators per drive row, one for reads and one for writes
- Each activity column should use the full current drive-row height and show the drive's relative share of visible read or write activity
- Activity indicators should light whole segments only; any non-zero activity lights the first segment and higher activity rounds up to additional fully filled segments
- Thin pill-shaped horizontal bars whose straight middle section shrinks naturally to zero for empty values
- Consistent alignment
- Compact rows

## Time and date (bottom right)

### Content

- Time (large): `10:43`
- Date (small): `2026-04-01`

### Layout

- Centered horizontally
- Time is the dominant visual element

## Behavior and data refresh

### Refresh rates

- Shared telemetry snapshot timer: `0.5 sec`
- CPU/GPU load: `0.5 sec`
- GPU vendor metrics: `0.5 sec`
- Network: `0.5 sec`
- Storage throughput: `0.5 sec`
- Storage drive usage: `0.5 sec`
- Clock: `0.5 sec`

### Units

- Dashboard labels and units are defined in `[metrics]`.
- Internal snapshot dump unit tokens remain `C`, `GHz`, `MHz`, and `RPM`.
