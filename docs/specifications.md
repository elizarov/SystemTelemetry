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
- Which cards and widgets to show in each row
- Which named board temperature and fan metrics to render through the layout bindings

### Config sources and persistence

- At runtime, the application must first load an embedded default `config.ini` resource, then read `config.ini` from the same directory as `SystemTelemetry.exe` when that file exists and overlay its values on top of the embedded defaults.
- The embedded `resources/config.ini` template must remain the single maintained source of truth for config-file entries, and [docs/layout.md](layout.md) must remain the single maintained source of truth for config-language syntax, section ownership, and examples.
- The config parser must accept only the documented current `resources/config.ini` key spellings and metric references; undocumented legacy aliases are not supported.
- Before writing `config.ini` beside the executable, the application must verify that the current process can write there; when it cannot, `Save Config` must prompt for elevation and complete the save through an elevated helper instance.
- The runtime executable must embed an application manifest that disables legacy file virtualization so `config.ini` reads and writes never fall back to a per-user `VirtualStore` shadow copy when the app is installed under `Program Files`.
- The runtime executable must also opt into per-monitor DPI awareness so Windows does not bitmap-scale a finished low-resolution dashboard surface on scaled displays.
- When `Save Config` creates `config.ini` beside the executable for the first time, it must begin from the embedded resource copy verbatim so the saved file keeps the same comments and documentation text before updating values in place.
- The runtime must rely on the embedded `resources/config.ini` template for shipped layout defaults.
- The `[display]` section must select the active dashboard layout by name through `display.layout`, and named dashboard size-and-card-placement definitions must live in `[layout.<name>]` sections.
- The shipped config template must define `800x480` as the default active layout and also include an experimental `480x800` portrait layout for the same panel resolution.
- The config overlay path must replace parsed layout expressions during overlay, so `config.ini` safely overrides `[layout.<name>]` and `[card.*]` layout trees without duplicating cards or widgets after save/reload cycles.

### Layout and rendering behavior

- The configured `layout.window`, display `position`, and layout geometry/font sizes must be treated as logical units that are converted to native device pixels using the current monitor DPI before rendering.
- When `display.monitor_name` targets a monitor that is not yet available during login startup, display-topology churn, or a temporary unplug, the UI must keep watching for that configured monitor instead of locking in a fallback monitor placement, and once the monitor is available it must apply the saved logical position there.
- When `[display] wallpaper` is set to an image path, the runtime must resolve relative paths beside `SystemTelemetry.exe` and apply that image as the wallpaper for the configured display through Windows per-monitor wallpaper APIs during startup and config reload, retrying once the configured display becomes enumerable if startup monitor discovery races behind login or hotplug.
- The layout engine must resolve row, card, and widget coordinates once after config load or reload and keep rendering in those static coordinates until the config is reloaded again.
- Card-local layout expressions may reference another card id as a reusable sub-layout, and the renderer must substitute that referenced card's layout during layout resolution instead of flattening it during config parse.
- The list of rendered cards must come from layout config.
- The storage drive list must come from the storage card's `drive_usage_list(...)` widget binding.
- The dedicated widget sections must derive metric-list and drive-usage row heights from measured UI font metrics plus dedicated bar-height and vertical-gap settings, so font-size experiments preserve or intentionally retune the visual rhythm.
- When a `metric_list` or `drive_usage_list` widget has less vertical space than its full configured content needs, the renderer must keep each header, row, bar, and configured gap at its full configured height and crop any overflow at the bottom instead of compressing the final visible lines.
- The dedicated `drive_usage_list` section must provide a drive-usage bar thickness setting so storage usage bars can be tuned independently from row height and from the thinner CPU/GPU metric bars.
- The dedicated `drive_usage_list` section must also provide one shared read/write activity-column width, the number of stacked activity segments, and the gap between those segments.
- The dedicated `drive_usage_list` section must provide separate gap controls for the activity-to-usage transition and the usage-bar-to-percent transition so the storage row alignment can be tuned without changing every column spacing together.
- The dedicated widget sections must own the widget-level geometry that affects visual rhythm, including metric bar thickness, throughput plot chrome sizes, gauge preferred size, and the fixed widths used by the storage drive row columns.
- The renderer must not rely on buried widget-spacing or widget-geometry pixel literals for text, footer, clock, gauge, or throughput sizing; those visual sizes must come from `config.ini` widget sections, with only non-visual safety clamps left in code.
- Widths that are fully determined by fixed renderer text such as throughput labels, throughput axis labels, drive-letter labels, and the `100%` drive percent column should be measured from the configured fonts at layout load and adjusted only by widget-section padding entries.
- The layout language must support a top-aligned stack mode that packs children at their preferred heights and leaves any remaining space below them, so lists such as drive usage rows do not have to stretch to fill the whole card column.
- In a regular vertical `stack(...)`, any resolved fixed-height widget must keep its preferred configured height and any remaining height reduction must be absorbed by flexible siblings such as throughput plots.
- The shipped `network_footer` and `spacer` widgets must use the same fixed preferred height.
- The renderer must obtain widget data through a separate metric-source abstraction that can provide text, gauge percentages, metric rows, throughput series, and drive rows by metric name.
- Metric-list rows and their retained recent-peak history series must use the same config-driven normalization ceilings so the live fill bar and peak ghost stay aligned after `[metric_scales]` changes.
- The renderer must support a blank rendering mode that preserves panel chrome, card titles, card icons, CPU and GPU names, drive labels, and empty chart or bar tracks while omitting dynamic metric text, time, date, plot lines, chart leaders, peak ghosts, gauge fill, and drive activity or usage fill.

### Runtime actions tied to config

- The popup menu must provide `Reload Config` before `Save Config` and immediately apply reloaded `config.ini` changes to the live dashboard so UI experiments can round-trip without restarting the app.
- The popup menu must provide a `Layout` submenu that lists every configured layout name, shows the active layout with a radio check, and on selection switches `display.layout`, reapplies the active named layout, and resizes the window immediately.
- The popup menu must provide a `Config To Display` submenu that lists every currently enumerable display by friendly name plus physical resolution, enables only displays whose resolution matches the dashboard's DPI-scaled window size for that display, and on selection must set `display.monitor_name`, set `display.position` to `0,0`, render a blank dashboard image to `telemetry_blank.png` beside the executable, set `display.wallpaper` to `telemetry_blank.png`, save the updated `config.ini`, and immediately apply that wallpaper to the selected display.
- The config reload path must tear down the active telemetry runtime before reinitializing vendor-backed telemetry providers so AMD GPU metrics continue working after save/reload round-trips.
- When `Reload Config` reapplies saved placement onto a monitor with a different DPI scale, it must preserve the configured logical window size without double-scaling the restored physical window bounds.
- The popup menu must expose an `Auto-start on user logon` toggle that shows a check mark only when the machine-wide `HKLM\Software\Microsoft\Windows\CurrentVersion\Run\SystemTelemetry` entry points to the running executable path, removes that entry when clicked while checked, and otherwise writes the running executable path there when clicked.
- When the current process cannot update that machine-wide `Run` entry directly, the auto-start toggle must prompt for elevation and complete the change through an elevated helper instance of the same executable.
- When `network.adapter_name` is left empty, auto-selection should prefer the active adapter that best represents the routed connection, favoring adapters with a usable default gateway and IPv4 address over host-only or otherwise unrouted virtual adapters.
- When `network.adapter_name` is set to a saved adapter name such as `Ethernet`, adapter selection should prefer an exact case-insensitive alias or description match and only fall back to substring matching when no exact match exists.
- The layout bindings `board.temp.<name>` and `board.fan.<name>` must be the only source of truth for which logical named board metrics are requested at runtime.
- The board provider must receive the set of requested logical board temperature and fan names by scanning all layout metric references that begin with `board.temp.` or `board.fan.`.
- The `[board]` section must map each requested logical `board.temp.*` or `board.fan.*` metric name to the board-specific sensor title that the active board provider uses for lookup.
- The `Save Config` action must persist the current auto-selected network adapter name alongside the display placement without adding any separate board-sensor selection state.

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
- Per-drive storage activity indicators should come from per-drive logical-disk I/O counters for the configured drive letters.
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
- Config To Display
- Auto-start on user logon
- Diagnostics
- Exit

The `Diagnostics` submenu must provide:

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
- Add a `Save Config` action that writes the current display identifier and relative X/Y placement back to the config file while preserving all other settings.
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
- Horizontal usage bars with rounded leading and trailing ends, with the straight middle section shrinking naturally to zero so empty values collapse to a circle the height of the bar
- Mini graphs for network and storage throughput activity
- Clean, minimal headers
- CPU and GPU load gauges must use the same fill color for their used arc as storage and other occupancy/usage bars use for their filled portion.
- CPU and GPU load gauges must render as a segmented ring that follows the `[gauge]` config geometry, with the shipped default using a 262 degree clockwise sweep split into 33 slots with a bottom-centered opening derived from that sweep and ring-slice segments separated by a small empty gap, filling clockwise from the lower-left toward the lower-right.
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
- Each graph must include a vertical axis and a small label showing the current max value used to scale that plot.
- Network and storage throughput plot scaling must be derived from the maximum value present in the retained history shown by that panel.
- The throughput plots should render a simple moving average over the most recent two readings so the line reflects a 1-second smoothing window, and the numeric throughput readout should show the same latest averaged point that the leader circle marks, yielding a per-last-second value that still refreshes every 0.5 seconds.
- Throughput plot scaling must use a minimum max of 10 MB/s, round the averaged max up in 5 MB/s steps through 100 MB/s, and then round it up in 50 MB/s steps above 100 MB/s.
- Throughput plots must reserve the configured `[throughput] plot_stroke_width` as top inset in the plotted area so a sample equal to the computed graph max still renders fully inside the chart bounds.
- Network plots should draw thin horizontal guide lines every 5 MB/s.
- Storage throughput plots should use the same averaged-history scaling, draw horizontal guide lines every 5 MB/s while the averaged max stays at or below 50 MB/s, and switch those guide lines to every 50 MB/s once the averaged max exceeds 50 MB/s.
- All throughput plots should also draw synchronized vertical time markers every 10 seconds, with marker placement driven from one shared snapshot-time phase so the markers line up and scroll in sync across network and storage.
- Throughput-plot horizontal and vertical markers should render with a dedicated darker config-driven marker color so they remain visually distinct from the throughput line itself.
- All throughput plots should render a small accent-colored leader circle centered on the right-most live point, with its diameter coming from the `[throughput]` widget config and the plotted line ending at that circle center so the live value reads clearly without labels on every point.
- Show adapter name and IP address together on the same final footer line when available.

## Storage panel (bottom center)

### Structure

- Stack read and write throughput sections on the left, with the same vertical rhythm and plot behavior used by the network panel, but at a slightly narrower width so the drive-usage list has more room.
- List the drives configured in the storage card's `drive_usage_list(...)` widget vertically on the right.

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
