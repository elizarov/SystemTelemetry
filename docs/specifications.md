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
- Before writing `config.ini` beside the executable, the application must verify that the process can write there; when it cannot, `Save Config` must prompt for elevation and complete the save through an elevated helper instance.
- The runtime executable must embed an application manifest that disables file virtualization so `config.ini` reads and writes target the executable-side file even when the app is installed under `Program Files`.
- The runtime executable must also opt into per-monitor DPI awareness so Windows does not bitmap-scale a finished low-resolution dashboard surface on scaled displays.
- `Save Config` must load the executable-side `config.ini` through the normal startup overlay path into a comparison copy, update only the keys whose live in-memory values differ, and write those updates back into the existing INI text so unchanged explicit overrides and unknown lines remain intact.
- When `Save Config` creates `config.ini` beside the executable for the first time, it must write only the keys whose live in-memory values differ from the embedded defaults.
- `Save Full Config To...` must export a complete config file by starting from the embedded `resources/config.ini` template text and updating every maintained config key with the live in-memory values so the exported file keeps the shipped line structure and comments.
- The runtime must rely on the embedded `resources/config.ini` template for shipped layout defaults.
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
- The dedicated widget sections must own the widget-level geometry that affects visual rhythm, including metric bar thickness, throughput plot chrome sizes, gauge ring and text geometry, and the fixed widths used by the storage drive row columns.
- The renderer must not rely on buried widget-spacing or widget-geometry pixel literals for text, footer, clock, gauge, or throughput sizing; those visual sizes must come from `config.ini` widget sections, with only non-visual safety clamps left in code.
- After the first layout pass resolves every gauge slot, the renderer must derive one shared fitted gauge radius from the most constrained resolved gauge bounds and use that same gauge size for every gauge render in the active layout.
- When gauge preferred height is needed before that shared fitted radius exists, it must be derived from the configured gauge text bottom offsets, ring thickness, outer padding, and measured gauge font metrics instead of a dedicated fixed-size config entry.
- Widths that are fully determined by fixed renderer text such as throughput axis labels, drive-letter labels, and the `100%` drive percent column should be measured from the configured fonts at layout load, with only the throughput axis width widened by its dedicated widget-section padding entry.
- Clock time and date preferred heights must come directly from the measured dedicated `[fonts].clock_time` and `[fonts].clock_date` metrics so small cards keep both lines visible without extra widget padding inflating the reservation.
- Text-widget preferred heights must come from the dedicated `[fonts].text` metrics plus `[text].preferred_padding`, `network_footer` preferred heights must come from the dedicated `[fonts].footer` metrics plus `[network_footer].preferred_padding`, and `vertical_spacer(widget_name)` preferred heights must match the preferred height of the referenced widget type.
- The layout language must use `vertical_spring` as an explicit flexible filler that absorbs the remaining height inside `rows(...)`, splits that height among multiple springs by weight, and lets layouts such as `rows(drive_usage_list, vertical_spring)` top-pack their content without a separate vertical-container kind.
- In a vertical `rows(...)` container, any resolved fixed-height direct child must keep its preferred configured height and any remaining height reduction must be absorbed by flexible non-spring siblings such as throughput plots when no `vertical_spring` is present.
- The shipped `vertical_spacer(network_footer)` layout pattern must reserve the same fixed preferred height as `network_footer` while drawing nothing.
- The renderer must obtain widget data through a separate metric-source abstraction that can provide text, gauge percentages, metric rows, throughput series, and drive rows by metric name.
- Metric-list rows and their retained recent-peak history series must use the same config-driven normalization ceilings so the fill bar and peak ghost stay aligned.
- The renderer must support a blank rendering mode that preserves panel chrome, card titles, card icons, CPU and GPU names, drive labels, and empty chart or bar tracks while omitting dynamic metric text, time, date, plot lines, chart leaders, peak ghosts, gauge fill, and drive activity or usage fill.

### Runtime actions tied to config

- The popup menu must provide `Reload Config` before `Save Config` and immediately apply reloaded `config.ini` changes to the live dashboard so UI experiments can round-trip without restarting the app.
- The popup menu must provide an `Edit layout` toggle that switches the dashboard into interactive layout-edit mode, shows that item checked while the mode is active, and lets the same menu item turn the mode back off.
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
- The config reload path must tear down the active telemetry runtime before reinitializing vendor-backed telemetry providers so AMD GPU metrics continue working after save/reload round-trips.
- When `Reload Config` reapplies saved placement onto a monitor with a different DPI scale, it must preserve the configured logical window size without double-scaling the restored physical window bounds.
- The popup menu must expose an `Auto-start on user logon` toggle that shows a check mark only when the machine-wide `HKLM\Software\Microsoft\Windows\CurrentVersion\Run\SystemTelemetry` entry points to the running executable path, removes that entry when clicked while checked, and otherwise writes the running executable path there when clicked.
- When the current process cannot update that machine-wide `Run` entry directly, the auto-start toggle must prompt for elevation and complete the change through an elevated helper instance of the same executable.
- When `network.adapter_name` is left empty, auto-selection should prefer the active adapter that best represents the routed connection, favoring adapters with a usable default gateway and IPv4 address over host-only or otherwise unrouted virtual adapters.
- When `network.adapter_name` is set to a saved adapter name such as `Ethernet`, adapter selection should prefer an exact case-insensitive alias or description match and only fall back to substring matching when no exact match exists.
- When `network.adapter_name` is set but does not match any current non-loopback, up adapter with an IPv4 address, runtime selection must fall back to the same auto-selection logic used when the setting is empty.
- The layout bindings `board.temp.<name>` and `board.fan.<name>` must be the only source of truth for which logical named board metrics are requested at runtime.
- The board provider must receive the set of requested logical board temperature and fan names by scanning all layout metric references that begin with `board.temp.` or `board.fan.`.
- The `[board]` section must map each requested logical `board.temp.*` or `board.fan.*` metric name to the board-specific sensor title that the active board provider uses for lookup.
- The `Save Config` action must persist the current auto-selected network adapter name alongside the display placement without adding any separate board-sensor selection state.
- The `Save Config` action must also persist the current `[storage] drives` selection.
- The `Save Config` action must also persist any in-memory layout weight edits made through interactive layout editing.
- While layout-edit mode is active, the renderer must draw thin config-colored separator guides over every resolved `rows(...)` and `columns(...)` split, highlight the hovered non-empty widget by outlining its resolved widget box, show any supported widget-local size guides inside that hovered widget, and update the cursor to the matching horizontal or vertical resize shape, or a crosshair for angular gauge editing, when the pointer hovers a draggable guide.
- While layout-edit mode is active, hovering a rendered widget text run or card title text run must also draw a light dotted highlight tightly around that run's measured bounds plus a small anchor at the run's upper-right corner, with dynamic text using the current draw-time bounds and static text using the cached layout-time measured bounds, and dragging that anchor left or right must live-update the shared configured font-size entry used by that run.
- While layout-edit mode is active, hovering a metric-list fill bar or drive-usage fill bar must also draw a light dotted highlight around that bar plus a small anchor centered on the bar's bottom edge, and dragging that anchor up or down must live-update the corresponding shared configured bar-height entry.
- While layout-edit mode is active, hovering a `gauge` widget must also draw a small diamond-shaped anchor centered on the gauge's top edge, and dragging that anchor in either axis must live-update `[gauge].segment_count`.
- While layout-edit mode is active, the `gauge` widget must also expose concentric circular size anchors centered on the gauge, with the outer circle live-updating `[gauge].outer_padding` and the inner circle live-updating `[gauge].ring_thickness`.
- While layout-edit mode is active, hovering a `gauge` widget anywhere inside its bounds must keep the `segment_count`, `outer_padding`, and `ring_thickness` anchors visible even when the pointer is currently over one of the gauge's other guides or anchor handles.
- Widget-local actionable hit-testing must use one shared maintained priority order that is visible in the layout-edit parameter metadata, with small actionable handles first, widget-local horizontal and vertical guides next, large circular gauge ring handles after those, and container split guides always last.
- While layout-edit mode is active, hovering any actionable widget-local guide or actionable edit-anchor handle that edits a specific config parameter must also show a standard Win32 tooltip that can extend beyond the dashboard window, with the first line formatted as `[section] parameter = current_value`, except that shown `[fonts]` anchor-handle tooltips must use the full `face,size,weight` font spec exactly in the config-file shape, and the second line showing a localized description looked up through extensible `config.section.parameter` keys from the embedded shared key=value localization catalog; the tooltip is shown only if pressing mouse down changes parameter, so it must follow the same hit-testing priority and same concrete edit target that the cursor and mouse-down logic use, it must never describe a different edit than the one that would start from the current pointer position, and non-actionable anchor highlight regions, hovered text-target highlight regions, and container split guides must not show that tooltip.
- Diamond-shaped segment-count anchors must use a tight hit region that stays at the rendered diamond bounds instead of inheriting the larger padded hit area used by circular anchors.
- While any layout-edit guide or edit anchor drag is active, the renderer must switch only that actively dragged guide or anchor highlight to the configured `[colors].active_edit_color` while keeping the existing stroke widths, dotted outlines, and anchor sizing unchanged.
- While layout-edit mode is active, the `throughput` widget must also expose circular size anchors for `[throughput].leader_diameter`, `[throughput].plot_stroke_width`, and `[throughput].guide_stroke_width`; each anchor circle must render slightly larger than the size it edits and dragging that circle in any direction must live-update the stored value from the pointer distance to the anchor center.
- Throughput circular size anchors must render as standalone circles without a dotted projected target rectangle, and their interactive region must stay at the circle outline plus the same slightly padded distance tolerance used by other circular anchors.
- Circular anchor handle hit-testing must follow the distance from the pointer to the rendered circle outline, using a small padded tolerance around that outline instead of a rectangular handle hit box.
- The highest-priority actionable-handle group must include the gauge and drive-usage diamond anchors, the small upper-right font-size anchors on hovered text and card titles, the metric-list and drive-usage bar-height anchors, and the throughput circular handles for `leader_diameter`, `plot_stroke_width`, and `guide_stroke_width`, so those handles win even when they overlap larger editable circles or guide regions.
- Horizontal guides in `rows(...)` must not be drawn next to direct fixed-height children or `vertical_spring` children whose size is not editable through row weights.
- Dragging a layout-edit guide must adjust the two adjacent child weights live, immediately relayout and repaint the dashboard, and reseed the dragged container's editable integer weights from the current resolved child extents when the drag begins, excluding any direct fixed-height row children and any `vertical_spring` children from that reseeded total.
- The `metric_list` widget must expose widget-local edit guides for `[metric_list].label_width` and `[metric_list].vertical_gap` while the widget is hovered in layout-edit mode, with the vertical guide at the label/value split and horizontal guides after each visible non-empty row so dragging either guide updates the shared metric-list chrome live without extending into empty missing-row space.
- The `metric_list` widget must treat `[metric_list].label_width` as the full left-side reservation up to the start of the value and bar region, with no separate metric-list gutter key.
- The `throughput` widget must expose widget-local edit guides for `[throughput].axis_padding` and `[throughput].header_gap` while the widget is hovered in layout-edit mode, with the vertical guide drawn at the plot-left boundary between the vertical scale gutter and the plotted area and the horizontal guide drawn at the header-to-graph boundary so dragging either guide relayouts that shared spacing live.
- The `throughput` widget must place its `leader_diameter` size anchor at the middle of the plot's right edge, its `plot_stroke_width` size anchor at the middle of the plot's left edge, and its `guide_stroke_width` size anchor on a centered horizontal plot guide whose center stays fixed while the edited stroke width changes so the handle stays visually stable.
- The `drive_usage_list` widget must expose widget-local vertical edit guides for `[drive_usage_list].label_gap` at the left edge of the `R` activity column, `[drive_usage_list].rw_gap` at the left edge of the `W` activity column, `[drive_usage_list].bar_gap` at the left edge of the usage bar, `[drive_usage_list].percent_gap` at the right edge of the usage bar, `[drive_usage_list].activity_width` at the right edge of the `W` activity column, and `[drive_usage_list].free_width` at the free-space column, together with horizontal guides for `[drive_usage_list].activity_segment_gap` spanning both activity columns at the top edge of the lowermost segment, `[drive_usage_list].header_gap` below the header, and `[drive_usage_list].row_gap` after each visible non-empty row so dragging those guides updates the shared spacing live without extending into empty missing-row space, and it must also expose one diamond anchor centered on the top edge of the read/write indicator band so dragging that anchor in either axis live-updates `[drive_usage_list].activity_segments`.
- While layout-edit mode is active, hovering a `drive_usage_list` widget anywhere inside its bounds must keep the `activity_segments` diamond anchor visible even when the pointer is currently over one of the drive-usage guides or anchor handles.
- The `drive_usage_list` activity-segment gap must clamp to the largest value that still leaves every stacked read/write segment at least one pixel tall, so dragging that guide cannot move past the last valid segment layout or produce inverted or negative-height visuals.
- The `gauge` widget must expose widget-local edit guides for `[gauge].sweep_degrees`, `[gauge].segment_gap_degrees`, `[gauge].value_bottom`, and `[gauge].label_bottom` while the widget is hovered in layout-edit mode, with short diagonal radial guides drawn across the ring at the current right-end arc boundary and at the end of the first segment so dragging the sweep guide edits only the gauge end side and dragging the first-segment guide edits the apparent first-segment width while converting that movement back into the stored segment gap, with horizontal guides at the value and label text bottoms so dragging those guides updates the text placements live, and it must also expose the top-edge diamond anchor that drags `[gauge].segment_count` in either axis plus concentric circle anchors for `[gauge].outer_padding` and `[gauge].ring_thickness`.
- While a layout-edit guide drag is active, the renderer must compare each affected descendant widget against every other widget of the same type in the active layout and draw paired measurement rulers on widgets whose dragged-axis size differs by no more than the configured threshold, while skipping `vertical_spring` widgets and any widgets whose row height is fixed.
- Once a dragged widget falls within the configured same-type size threshold that makes the ruler visible, the drag must snap to the nearest exact same-size group chosen from the drag-start layout, solving the snapped guide weights through iterative relayout passes so nested weighted hierarchies can converge on the matching extent; when multiple groups are equally close at drag start, the first group in layout order wins.
- When multiple same-type widgets in one vertical or horizontal stack share the same dragged-axis extent by construction, the measurement ruler for that type and extent must draw only on the first widget in that stack, both for the widgets being edited and for matching widgets elsewhere in the active layout.
- When same-type widgets match exactly on the dragged axis, the measurement ruler must show centered notch markers, with the notch count coming from that exact-match type's active drag ordinal where a type is the widget type plus the matched dragged-axis extent.
- Holding `Alt` during a layout-edit drag must temporarily disable same-size snapping while keeping the free-drag weight adjustment active.
- Pressing `Esc` must exit either move mode or layout-edit mode.

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
- The Gigabyte motherboard telemetry path should identify Gigabyte boards, discover the installed SIV location from the Windows registry, load the required Gigabyte SIV .NET assemblies in-process from native C++ code, initialize the vendor hardware-monitor module against the `HwRegister` source through reflection, collect the available fan RPM and temperature readings directly from those loaded assemblies, and match requested logical `board.temp.*` and `board.fan.*` names through the configured `[board]` sensor-title mapping.

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
- The overlay content should be easy to copy into configuration.
- The application must also read the saved relative X/Y coordinates from configuration at startup and place the window accordingly.
- The application must provide a `Save Config` action that writes the display identifier and relative X/Y placement back to the config file while preserving all other settings and unchanged explicit overrides.
- The `Save full config to...` action must open a standard Windows Save dialog, default to the current working directory, default the file name to `telemetry_full_config.ini`, and export the full embedded-template-shaped config with current live values.
- The `Save dump to...` action must open a standard Windows Save dialog, default to the current working directory, default the file name to `telemetry_dump.txt`, and write the same text dump format used by diagnostics output.
- The dump format contains only the snapshot fields that `/fake` loads and the dashboard renders; provider-debug details remain trace-only diagnostics data.
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
- Gauge fill must quantize to whole pills only; any usage above 0 percent lights the first pill, additional pills round up from the usage percentage, and partially filled pills must not be drawn.
- CPU and GPU load gauges must also overlay a small translucent max-ghost on the single segmented pill that corresponds to the highest retained load ratio seen in the shared recent 30 second history window.
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

- CPU: `GHz`
- Memory: `GB`
- Network: `MB/s`
- Storage: `GB/TB`
