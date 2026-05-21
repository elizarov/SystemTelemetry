# CaseDash Glossary

This document owns canonical project terminology. Use these terms in project docs, comments, release notes, and user-visible copy unless a source identifier, config token, switch, trace prefix, benchmark name, or menu label requires a different exact spelling.

See also: [docs/project.md](project.md) for documentation ownership, [docs/layout.md](layout.md) and [resources/config.ini](../resources/config.ini) for config language syntax and shipped token spellings, [docs/diagnostics.md](diagnostics.md) for diagnostics switches and trace prefixes, and [docs/hardware.md](hardware.md) for provider requirements.

## Spelling Rules

| Context | Use | Notes |
|---|---|---|
| Prose feature name | layout-edit mode | Use `layout-edit` as the compound adjective for the feature, interaction model, guides, affordances, traces, and benchmarks. |
| User-visible command | `Edit Layout` | This is the popup-menu mode toggle. |
| User-visible editor command | `Layout Editor...` | This menu item starts layout-edit mode when needed and opens the `Edit Configuration` window. |
| User-visible editor window | `Edit Configuration` window | Use `modeless editor` only when discussing modeless behavior generally. |
| Diagnostics switch | `/edit-layout` | Use `/edit-layout:<widget-name>`, `/edit-layout:horizontal-sizes`, and `/edit-layout:vertical-sizes` for modifiers. |
| Prose feature name | layout guide sheet | Use this for the generated diagnostics image. |
| Command-line and benchmark names | `/layout-guide-sheet`, `layout-guide-sheet` | Use the hyphenated form only where the switch, benchmark name, filename stem, trace field, or generated id requires it. |
| Config and source identifiers | `[layout_guide_sheet]`, `layout_guide_sheet` | Use the underscore form only for config sections, package names, source paths, and symbols. |
| Prose icon name | app icon | Use `/app-icon` for the diagnostics switch and `app_icon` for source identifiers. |
| Prose dump name | snapshot dump | Use `dump` alone only after the snapshot dump has already been named in nearby text or when referring to the `/dump` switch. |
| Synthetic telemetry source | built-in synthetic telemetry | Use `fake runtime`, `fake provider`, and `/fake` for switch, architecture, benchmark, or source-code contexts. Avoid `fake data` in docs. |
| FPS metric | presented FPS | Use `presented-FPS` only as a compound adjective, such as `presented-FPS collection`. |
| Active area term | active region | Use `active-region` only as a compound adjective, such as `active-region trace`. |
| Edit target term | edit target | Use `editable target` only when describing whether a visible item can be edited. |
| Service concept | CaseDash service | The installed Windows service and pipe are named `CashDashService`; preserve that exact spelling for service-name and source-symbol references. |

## Product And Runtime

| Term | Meaning |
|---|---|
| CaseDash | The app and project name. |
| dashboard | The live always-on telemetry window rendered by `CaseDash.exe`. |
| live dashboard | The interactive dashboard UI path, as opposed to headless diagnostics or deterministic offscreen renders. |
| dashboard UI | The normal interactive shell with popup menus, tray icon, telemetry updates, and rendering. |
| tray icon | The notification-area icon exposing the same menu action set as the dashboard menu. |
| popup menu | The right-click dashboard or tray menu. |
| move mode | The pointer-attached placement mode that overlays monitor name, effective scale, and relative coordinates. |
| runtime config | The loaded embedded config plus the executable-side config overlay and any live in-memory edits. |
| embedded template | The `resources/config.ini` defaults compiled into the executable. |
| executable-side config | The `config.ini` file beside `CaseDash.exe`. |
| executable-side overlay | Values loaded from executable-side `config.ini` over the embedded template. |
| live values | The current runtime values after config loading, runtime selection, auto-detection, and unsaved edits. |
| explicit scale override | A configured render scale that replaces monitor DPI-derived scale. |
| target monitor | The configured display selected by `[display] monitor_name`. |
| placement | The dashboard window position relative to the target monitor. |
| fullscreen display placement | A `Configure Display` choice that fits the layout to the whole selected display and owns the CaseDash blank wallpaper for that monitor. |
| edge display placement | A `Configure Display` top, bottom, left, or right choice that fits the layout to one screen edge without owning a wallpaper. |
| `Start with Windows` | The user-visible command that manages machine-wide logon startup and the `CashDashService` service. |
| `Configure Display` | The command that chooses a fullscreen or edge display placement, computes explicit scale and position, and applies CaseDash wallpaper only for fullscreen placements. |
| `Bring to Front` | The command that raises the dashboard. |
| `About CaseDash` | The dialog that shows the themed app icon, version, build kind, and commit when available. |

## Configuration And Layout

| Term | Meaning |
|---|---|
| config language | The INI-based language owned by [docs/layout.md](layout.md). |
| named layout | A `[layout.<name>]` section selected by `[display] layout`. |
| active named layout | The named layout currently selected by config or `/layout:<name>`. |
| named theme | A `[theme.<name>]` section selected by `[display] theme`. |
| active named theme | The named theme currently selected by config or `/theme:<name>`. |
| theme token | One of `background`, `foreground`, `accent`, or `guide` inside a `[theme.<name>]` section. |
| color role | A `[colors]` or `[layout_guide_sheet]` entry that resolves to a literal or derived color. |
| derived color expression | A theme-relative color expression using `rotate_hue`, `mix`, and `alpha`. |
| metric registry | The `[metrics]` section that defines widget-facing labels, units, and normalization. |
| metric id | A case-sensitive metric key such as `cpu.load` or `gpu.fps`. |
| logical board metric | A configured `board.temp.*` or `board.fan.*` metric selected by layout references. |
| board binding | A `[board]` mapping from a logical board metric to a provider sensor name. |
| runtime selection section | `[display]`, `[gpu]`, `[network]`, or `[storage]`. |
| card section | A `[card.<id>]` section containing a title, icon, and inner layout expression. |
| card id | A case-sensitive card identifier such as `cpu` or `storage_usage`. |
| container kind | `rows(...)` or `columns(...)`. |
| container weight | A relative child weight written as `name:weight` or `name:weight(...)`. |
| layout tree | The nested card, container, and widget expression resolved from a named layout or card section. |
| widget parameter | The inline payload written as `widget(...)`. |

## Shipped Config Tokens

`resources/config.ini` is the authoritative spelling source. This section repeats the high-value tokens so docs can avoid naming drift.

| Token group | Tokens |
|---|---|
| Container kinds | `rows`, `columns` |
| Widget names | `text`, `gauge`, `metric_list`, `throughput`, `network_footer`, `vertical_spacer`, `vertical_spring`, `drive_usage_list`, `clock_time`, `clock_date` |
| Icon names | `cpu`, `gpu`, `network`, `storage`, `time` |
| Shipped layout ids | `5x3`, `3x5`, `4x1` |
| Shipped card ids | `cpu`, `gpu`, `network`, `storage`, `storage_throughput`, `storage_usage`, `time` |
| Theme ids | `dark_cyan`, `dark_green`, `midnight_violet`, `amber_crt`, `volcanic`, `rose_gold`, `laser_grid`, `monochrome_lime`, `ultraviolet_black`, `copper_black`, `blueprint_light`, `paper_ink`, `mint_light`, `lavender_day`, `sunrise_light`, `slate_gold_light`, `sage_light`, `rose_light`, `cobalt_white`, `plum_white` |
| Built-in metric ids | `cpu.load`, `cpu.clock`, `cpu.ram`, `gpu.load`, `gpu.temp`, `gpu.clock`, `gpu.fan`, `gpu.fps`, `gpu.vram`, `network.upload`, `network.download`, `storage.read`, `storage.write`, `drive.activity.read`, `drive.activity.write`, `drive.usage`, `drive.free` |
| Logical board metric ids | `board.temp.cpu`, `board.fan.cpu`, `board.fan.gpu`, `board.fan.system` |
| Runtime-only placeholder | `nothing` |

## Widget And Card Terms

| Term | Meaning |
|---|---|
| widget | A drawable configured dashboard element with a supported widget name. |
| widget-local geometry | Geometry parameters owned by a widget section, such as `[gauge]` or `[metric_list]`. |
| `text` widget | A single metric text widget. |
| `gauge` widget | A segmented load gauge bound to one metric id. |
| `metric_list` widget | A list of metric rows with labels, pill bars, values, optional annotations, and row ordering. |
| `throughput` widget | A retained-history throughput chart with a current value header and live leader. |
| `network_footer` widget | The selected network adapter and IPv4 footer line. |
| `drive_usage_list` widget | Per-drive activity and usage rows. |
| `clock_time` widget | The configured-format local time readout. |
| `clock_date` widget | The configured-format local date readout. |
| `vertical_spacer` widget | A non-drawing spacer that reserves another widget type's preferred height. |
| `vertical_spring` widget | A flexible spacer that absorbs remaining `rows(...)` height. |
| card | A dashboard panel named by the active layout and configured by a `[card.<id>]` section. |
| card chrome | Shared card border, radius, padding, header icon, title, and header spacing. |
| card header content | The optional title and icon area at the top of a card. |
| pill bar | The rounded horizontal fill used by metric rows and drive-usage rows. |
| throughput plot | The scrolling retained-history line chart inside a throughput widget. |
| live leader | The current-value tail segment of a throughput plot. |
| guide line | A throughput graph reference line or layout-edit sizing guide line, depending on context. |
| warning indicator | The short warning-colored text `!admin`. |
| unavailable value | A metric value that renders as an empty track or `N/A` depending on display context. |

## Layout-Edit Terms

| Term | Meaning |
|---|---|
| layout-edit mode | The interactive dashboard mode that exposes layout-edit guides, hover affordances, drag behavior, and the modeless editor. |
| edit session | The in-memory edit state that can be saved or discarded. |
| dirty edit session | An edit session with unsaved changes. |
| unsaved-session prompt | The shared save, discard, or cancel prompt shown before destructive loss of dirty edit-session state. |
| modeless editor | The `Edit Configuration` window when discussing its non-blocking window behavior. |
| config tree | The tree in the `Edit Configuration` window that follows config order. |
| editor pane | The right-hand editing area in the `Edit Configuration` window. |
| per-field revert | A control that restores one field to its session baseline. |
| preview | Immediate live application of a valid editor value before `Save Config`. |
| edit target | A configurable target resolved from layout-edit metadata or active regions. |
| active region | A renderer-produced mouse-reactive area used by live hit testing, traces, screenshots, and layout guide sheet callouts. |
| hover-equivalent state | The visual state that matches what live layout-edit hover would show for a target. |
| edit affordance | Any visible guide, handle, anchor, outline, cursor, or target marker exposed by layout-edit mode. |
| container guide | A row or column guide for editing container sizing and child boundaries. |
| split guide | A guide for a row or column split between layout children. |
| gap handle | A draggable handle for dashboard, card, widget, or in-card spacing. |
| reorder handle | A handle that reorders metric-list rows or layout children. |
| text anchor | A target anchor for text size or placement edits. |
| color target | A target that edits a configured color role or color parameter. |
| dotted outline | The dotted hover outline for actionable text, icon, pill bar, or metric-definition targets. |
| tree-selection highlight | The dashboard highlight that follows the selected config tree node. |
| active dashed outline | The dashed outline for the actively dragged row or layout child. |
| drag replay | The overlay-layer replay of dragged row or child content during a drag. |
| tooltip parameter line | The first line of a layout-edit tooltip or callout, naming the config shape or edit target. |
| tooltip description line | The second line of a layout-edit tooltip or callout, containing localized target help text. |
| board `Binding` selector | The editor control that selects provider sensor names for supported board metrics and fallback-backed GPU metrics. |

## Layout Guide Sheet Terms

| Term | Meaning |
|---|---|
| layout guide sheet | A diagnostics PNG that explains editable geometry for the selected layout. |
| compact screen overview | The packed dashboard overview rendered before representative cards. |
| representative card | A selected card that demonstrates one or more widget types present in the active named layout. |
| annotated item | The overview or representative card being surrounded by callouts. |
| callout | A tooltip-style explanation attached to a representative target. |
| help bubble | The rounded two-line text bubble inside a callout. |
| leader line | The thin straight line connecting a representative target to a help bubble. |
| side stack | A left or right stack of callouts beside an annotated item. |
| promoted top callout | A callout moved above the annotated item from a side stack. |
| promoted bottom callout | A callout moved below the annotated item from a side stack. |
| representative target area | The specific active region or anchor chosen for one callout. |
| callout placement | The deterministic layout step that places bubbles and leader lines without collisions. |
| placement warning | Trace detail emitted when every bubble cannot be placed without overlap. |
| diagnostics error panel | The rendered error panel used when no representative card content is available. |

## Telemetry And Hardware Terms

| Term | Meaning |
|---|---|
| telemetry runtime | The `TelemetryRuntime` service that collects and publishes snapshots on the shared 250 ms cadence. |
| telemetry snapshot | A copied point-in-time runtime model consumed by rendering, diagnostics dumps, and fake reloads. |
| telemetry cadence | The shared 250 ms collection cadence. |
| Windows-native telemetry | CPU, memory, network, storage, drive activity, clock, and presented-FPS data collected without vendor SDKs. |
| hardware provider | A provider that adds GPU or board metrics when matching hardware and runtime dependencies are available. |
| GPU provider | A provider selected from the configured unique non-software DXGI adapter identity. |
| GPU adapter selection name | The runtime GPU selector and `[gpu] adapter_name` value. It is the raw DXGI display name when unique, or the display name plus a stable ` #N` suffix when multiple unique adapters share that name. |
| board provider | A provider selected from the baseboard manufacturer. |
| vendor provider | A supported GPU or board provider created after vendor mapping. |
| unsupported GPU provider | The fallback GPU provider that keeps provider-owned metrics unavailable when no supported GPU provider matches. |
| unsupported board provider | The fallback board provider that keeps board metrics unavailable when no supported board provider matches. |
| provider sensor name | A provider-specific board sensor name used by `[board]` bindings. |
| provider diagnostics | Provider-specific trace details and status fields used for troubleshooting. |
| fake provider | The provider used by `/fake` and benchmark paths. |
| built-in synthetic telemetry | The deterministic or live synthetic snapshot source used when `/fake` has no path. |
| dump-backed snapshot | The snapshot reloaded from a `/fake:<path>` file. |
| presented FPS | FPS from Windows present-event telemetry and process selection. |
| native FPS | A driver-supplied FPS value used only as a fallback where documented. |
| ETW collection | Local Event Tracing for Windows collection for presented FPS. |
| CaseDash service | The privileged collection service used before falling back to local ETW collection. The exact service and pipe name is `CashDashService`. |
| metric catalog | Telemetry package logic that adapts snapshots and metric definitions into widget-facing values, histories, drive rows, and formatted text. |
| retained history | Stored recent samples used by charts, peak ghosts, and animation. |
| raw scalar retained history | The 120-sample, 250 ms scalar history for bars and gauges. |
| compact throughput retained history | The 30-point one-second history for throughput charts. |
| throughput live window | The four raw 250 ms samples used for the throughput live leader. |
| permission-gated value | A value blocked by access rights, surfaced with `permission_required` in dumps and `!admin` where the UI can still explain the state. |
| provider-owned value | A value supplied by a hardware provider rather than the Windows-native collector. |
| runtime target resolution | Runtime selection of GPU adapter, network adapter, storage drives, and board provider. |

## Supported Providers

| Provider | Canonical description |
|---|---|
| AMD ADLX | Radeon GPU telemetry through AMD ADLX. |
| Intel Level Zero Sysman | Intel GPU telemetry through the Level Zero Sysman loader. |
| NVIDIA NVML | NVIDIA GPU telemetry through NVML. |
| ASUS Armoury Crate | ASUS board telemetry through Armoury Crate support libraries. |
| ASUS System Control Interface ATKACPI | ASUS board telemetry through the `\\.\ATKACPI` device. |
| MSI Center SDK | MSI board telemetry through MSI Center SDK. |
| Gigabyte SIV | Gigabyte board telemetry through Gigabyte SIV assemblies. |

## Diagnostics Terms

| Term | Meaning |
|---|---|
| diagnostics CLI | The command-line diagnostics surface owned by [docs/diagnostics.md](diagnostics.md). |
| headless diagnostics run | A diagnostics run with `/exit` that exports requested outputs once and exits. |
| UI-attached diagnostics mode | A diagnostics run without `/exit` that keeps requested outputs refreshed while the UI runs. |
| diagnostics output | Any requested trace, snapshot dump, screenshot, layout guide sheet, app icon, config export, crash report, or minidump. |
| trace output | Continuous UTF-8 diagnostic text produced by `/trace`. |
| trace prefix | A supported prefix-filter name such as `profile` or `renderer`; the complete list lives in [docs/diagnostics.md](diagnostics.md). |
| snapshot dump | The machine-parseable runtime snapshot exported by `/dump`. |
| screenshot | The rendered dashboard PNG exported by `/screenshot`. |
| app icon export | The runtime-rendered themed icon PNG exported by `/app-icon`. |
| minimal config overlay export | The live values that differ from the loaded executable-side config state, exported by `/save-config`. |
| full config export | The embedded-template-shaped live config exported by `/save-full-config`. |
| blank rendering mode | The `/blank` mode that keeps dashboard chrome and static labels while omitting dynamic values and fills. |
| default-config behavior | `/default-config` suppressing the executable-side config overlay for the current process. |
| hover diagnostics | `/hover:<x>,<y>` resolving the layout-edit target and tooltip text for a screenshot export. |
| active-region trace | `diagnostics:active_region` lines emitted for mouse-reactive regions in traced screenshot exports. |
| crash report | The best-effort text report written after an unhandled native process crash. |
| minidump | The `.dmp` crash dump written with a crash report. |

## Rendering And Animation Terms

| Term | Meaning |
|---|---|
| renderer | The D2D-free rendering interface and backend package. |
| dashboard renderer | The package that resolves dashboard scenes, widget layout, active regions, and live presentation frames. |
| render space | Renderer-facing coordinate and geometry DTOs. |
| logical units | Runtime UI geometry units that are converted to device pixels by DPI scale or explicit scale. |
| device pixels | The physical pixel coordinates used by rendering targets. |
| deterministic render | A non-interpolated offscreen render for screenshots, layout guide sheets, app icon exports, validation, tests, or blank mode. |
| snapshot layer | The opaque live layer containing static dashboard content and text. |
| overlay layer | The optional transparent live layer containing layout-edit affordances, drag replay, and move overlay content. |
| live-layer bitmap | A `RenderBitmapStorage::LiveLayer` resource used by live presentation. |
| animation command list | Immutable widget animation commands collected while drawing snapshot or overlay layers. |
| animation data key | The stable logical key that lets animation follow data across layout changes. |
| animation timeline | Render-thread state that creates transitions, samples active animation, and prunes stale tracks. |
| render thread | The live presentation thread that owns HWND presentation, retained frame state, and the animation timeline. |
| main thread | The UI thread that owns input, menus, config mutation, layout resolution, deterministic rendering, and live layer construction. |
| dirty bounds | Conservative animation redraw bounds used for retained composition. |
| dirty-window composition | The retained rendering path that restores only dirty regions for animation-only frames. |
| immediate-present path | Benchmark rendering that uses the production backend without waiting for vsync. |
| vsync-paced animation | Live animation paced by presentation instead of a fixed render-thread timer. |

## Build, Release, And Website Terms

| Term | Meaning |
|---|---|
| `build.cmd` | The maintained build entrypoint. |
| `test.cmd` | The maintained CTest entrypoint. |
| `format.cmd` | The maintained C++ formatting entrypoint. |
| `lint.cmd` | The maintained source policy and architecture check entrypoint. |
| `package.cmd` | The maintained local MSI packaging entrypoint. |
| `web-build.cmd` | The maintained static website build entrypoint. |
| `profile_benchmark.cmd` | The maintained elevated profiling and daemon-backed benchmark entrypoint. |
| `release.cmd` | The maintained local release preparation entrypoint. |
| `CaseDashBenchmarks` | The benchmark executable. |
| benchmark name | The first `CaseDashBenchmarks.exe` argument, such as `edit-layout` or `layout-guide-sheet`. |
| generated website output | `web/dist/`. |
| generated website assets | Dashboard screenshots, layout guide sheets, app icons, and theme metadata emitted by `web-build.cmd`. |
| GitHub social preview | The generated 1280 x 640 repository preview image. |
| release chunk | One machine-readable release-note section in [docs/changelog.md](changelog.md). |
| official release build | A clean build from an exact `v<VERSION>` tag. |
| development build | A non-tagged or dirty build reporting development metadata. |
| WiX MSI | The per-machine installer built from `installer\`. |

## Architecture Package Names

Use package names exactly as directory names when discussing source boundaries:

- `config`
- `dashboard`
- `dashboard_renderer`
- `diagnostics`
- `display`
- `layout_edit`
- `layout_edit_dialog`
- `layout_guide_sheet`
- `layout_model`
- `main`
- `renderer`
- `telemetry`
- `util`
- `vendor`
- `widget`
