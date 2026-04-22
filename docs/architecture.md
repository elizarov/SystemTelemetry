# System Telemetry Architecture

This document owns code structure, subsystem boundaries, major runtime flows, and build-graph shape.
See also: [docs/specifications.md](specifications.md) for normative product behavior, [docs/diagnostics.md](diagnostics.md) for diagnostics contracts, [docs/layout.md](layout.md) for config language, and [docs/build.md](build.md) for developer setup and commands.

## Top-Level Map

- `src/` contains the runtime application, configuration, telemetry, rendering, diagnostics, and layout-edit implementation.
- `src/config/` contains the config model, parser, resolver, writer, schema metadata, config-facing enum and DTO contracts, and injected metric-catalog view.
- `src/display/` contains monitor enumeration, DPI scaling, placement, configure-display, wallpaper application helpers, and display-owned constants.
- `src/diagnostics/` contains diagnostics session and headless-run orchestration, command-line option parsing, default diagnostics output filenames, snapshot dump I/O, and diagnostics-owned support modules.
- `src/main/` contains the application entry point, runtime config I/O, login auto-start registry updates, elevation handoff, and main-process constants.
- `src/widget/widget.*` owns the widget interface and factory, and `src/widget/impl/` contains the concrete widget draw and layout-state modules used by the renderer.
- `src/util/` contains pure shared utilities for paths, command-line text, string helpers, enum string conversion, UTF-8 conversion, embedded resource loading, localization catalog access, numeric safety, and trace emission.
- `src/dashboard/` contains the dashboard application, controller, shell UI, dashboard command and timer constants, menu types, and shared layout-edit overlay state.
- `src/dashboard_renderer/dashboard_renderer.*` owns the renderer boundary, `src/dashboard_renderer/render_types.*` owns shared render-space contract types, and `src/dashboard_renderer/impl/` contains helper modules such as palette conversion, palette lookup, Direct2D caches, text measurement caches, and layout resolution state.
- `src/layout_edit/` contains shared layout-edit interaction, parameter, tooltip, tree, trace-session, and snap-solver modules.
- `src/layout_edit_dialog/layout_edit_dialog.*` owns the modeless `Edit Configuration` window boundary, and `src/layout_edit_dialog/impl/` contains its internal dialog modules.
- `src/telemetry/telemetry.*` owns the telemetry collector boundary, `src/telemetry/metrics.*` owns the single production metric catalog and adapts snapshots and metric definitions into widget-facing metric values, `src/telemetry/metric_types.h` owns telemetry snapshot enums, `src/telemetry/board/` and `src/telemetry/gpu/` contain vendor-provider bridges, and `src/telemetry/impl/` contains collector submodules plus system-info support for CPU, GPU, board, network, storage, and fake-runtime support.
- `resources/` contains the resource script, embedded config and localization files, dialog templates, manifest, and image assets.
- `tests/` contains unit tests for config, layout resolution, retained-history behavior, and the native benchmark host.
- `tools/` contains shared formatting, lint, tidy, profiling, and source dependency graph helper scripts.

## Layered Core

- The core project layers are ordered `util` -> `config` -> `telemetry` -> application-facing packages such as dashboard, renderer, widget, diagnostics, display, layout-edit, and main.
- Dependencies flow downward only. A higher layer may include lower-layer contracts, but a lower layer must not include or call into a higher layer.
- `src/util/` is the base layer. It contains domain-neutral helpers for text, paths, resources, enum strings, UTF-8 conversion, localization catalog access, numeric safety, and trace emission. Util modules may depend on other util modules, but must not depend on config, telemetry, rendering, UI, diagnostics, or application packages.
- `src/config/` is the second layer. It owns the persisted config model, parser, writer, resolver, schema metadata, and config-facing contract types such as widget class, metric display style, and telemetry settings DTOs. Config modules may depend only on config and util modules.
- Config must not duplicate runtime catalogs or reach upward to validate runtime concepts. When config parsing needs runtime knowledge, it uses config-owned injection contracts such as `ConfigMetricCatalog`; production code supplies the telemetry-backed implementation from above.
- `src/telemetry/` is the third layer. It owns live collection, fake collection, snapshot and dump-facing telemetry types, provider bridges, retained history, and the single production metric catalog. Telemetry modules may depend on telemetry, config, and util modules, but must not depend on renderer, widget, dashboard, diagnostics, display, layout-edit, or main modules.
- Telemetry is allowed to consume config contracts such as telemetry settings and metric display style, and it publishes runtime contracts such as `TelemetryCollector`, `SystemSnapshot`, provider samples, and metric resolution for higher packages.
- Cross-layer shared types belong in the lowest layer that semantically owns them. Move config-language DTOs to config, runtime telemetry DTOs to telemetry, and domain-neutral helpers to util; do not copy catalogs or enums across layers to avoid a dependency violation.
- `lint.cmd` enforces the util, config, and telemetry layer rules through the source dependency graph before reporting success.

## Major Subsystems

### App shell and session orchestration

- `DashboardApp` owns HWND lifetime, message dispatch, tray integration, repaint invalidation, move-mode presentation, and the shell side of layout-edit interaction.
- `DashboardController` owns active config state, runtime instance lifetime, diagnostics session lifetime, save and reload actions, layout and scale switching, runtime target selection, and layout-edit session state.
- `DashboardShellUi` owns popup-menu construction, focused command dispatch, custom prompts, and the host bridge for the modeless editor window.

### Config model and persistence

- The config subsystem parses the embedded template first, overlays executable-side config, resolves the active named layout, and writes either minimal overlay saves or full embedded-template-shaped exports.
- `LayoutConfig` is the layout-edit session boundary and includes layout-owned sections such as widget geometry, shared styling, board bindings, and metric definitions.
- `AppConfig` layers runtime display, network, storage, and active-layout selection around the shared layout-owned state.
- Runtime-only placeholder metric metadata stays synthesized outside persisted config so `[metrics]` remains limited to configurable metric definitions.
- Config metric validation uses an injected catalog view; the metric id and display-style source of truth lives in telemetry metrics.
- Parser and writer code keep UTF-8 I/O, overlay behavior, and text-preserving saves separate from higher-level config resolution.

### Telemetry

- `TelemetryCollector` owns steady-state snapshot refresh, provider composition, and runtime target resolution for network and storage.
- Windows-native collection covers generic CPU, memory, network, storage, and clock data.
- Vendor providers extend that collector with AMD GPU support and the Gigabyte board-metric path without changing the renderer-facing snapshot model.
- Board telemetry keeps the last discovered provider sensor-name lists cached alongside live samples so layout-edit binding pickers stay populated across transient board-sample gaps.
- Fake-runtime support bypasses live providers and serves either the built-in synthetic snapshot or a reloadable dump-backed snapshot.

### Rendering and layout resolution

- `DashboardRenderer` owns static layout resolution, renderer resource lifetime, icon loading, text measurement, live window rendering, and screenshot export rendering.
- Shared render-space contract types isolate the rest of the codebase from low-level Direct2D and DirectWrite structs.
- Widget draw modules refer to colors by render color id; `DashboardRenderer` keeps the resolved RGBA palette private and maps ids to colors internally.
- Widget modules own widget-local preferred-size logic, draw behavior, and layout-edit artifact registration.
- `MetricSource` adapts `SystemSnapshot` into widget-facing values, histories, drive rows, and formatted text while caching per-frame derived results.

### Layout editing

- `LayoutEditController` owns hover state, active drags, hit-testing, capture, cursor choice, and drag-session flow.
- Layout-edit parameter metadata and helpers centralize editable target identity, config-path mapping, clamps, shared tooltip text, and preview application.
- `LayoutEditDialog` owns the modeless editor window, config-tree selection, right-pane editing, and preview or revert flow, with focused helper modules under `src/layout_edit_dialog/impl/`.
- The renderer exposes the resolved guide and anchor geometry used both by live interaction and by diagnostics screenshot validation.

### Diagnostics

- The diagnostics subsystem parses diagnostics CLI switches, manages headless `/exit` runs, owns requested output exports, creates the top-level trace session when trace is enabled, and decides whether telemetry initialization failures are reported through trace or modal UI.
- `src/util/trace.*` owns trace line emission plus generic trace value formatting and quoting helpers. Runtime flows create a top-level `Trace` object and pass it by non-null reference; disabled output and stream-null handling stay inside `Trace`.
- Snapshot dumps and fake-runtime imports share the same dump serializer and parser.
- Diagnostics screenshot export uses the same renderer scene, layout-edit hover resolver, and tooltip text builder as the live window path instead of a separate rendering implementation.

## Runtime Flows

### Startup and config flow

- `src/main/main.cpp` initializes process-wide shell settings, parses command-line options, and chooses either the normal UI path or the headless diagnostics path.
- Config load starts from embedded `resources/config.ini`, applies the executable-side overlay unless suppressed, and resolves the active layout plus runtime selections before telemetry and rendering start.
- The executable manifest disables file virtualization and keeps config reads and writes pointed at the executable-side location.

### Telemetry flow

- The controller owns one runtime collector instance.
- Each refresh produces a `SystemSnapshot` that becomes the renderer input for live paint and diagnostics export.
- Runtime network and storage selections are resolved from current machine candidates and surfaced back to menu-building code for user selection.

### Render and layout flow

- Layout resolution converts the active config into static dashboard, card, and widget rectangles.
- Draw-time metric binding uses the latest snapshot plus the current metric registry.
- Live paints and screenshot exports share the same renderer-owned layout resolution and widget draw code.
- Layout, scale, reload, and configure-display actions reconfigure the live renderer in place and force a dashboard repaint before any follow-on modeless layout-editor refresh work runs.
- While the modeless layout editor stays open, its tree refreshes rebuild directly from the current live config and the shared uncached tree-model builder.

### Diagnostics flow

- The diagnostics path optionally reloads config through the same live reload logic used by the dashboard.
- Requested outputs write trace, dump, screenshot, minimal-config, or full-config artifacts using the same runtime state the live app would use.
- `/exit` performs one update-and-export pass and exits without joining the normal single-instance UI lifetime.

### Layout-edit flow

- The shell forwards pointer events into the layout-edit controller, which resolves actionable targets from renderer-provided guide and anchor data.
- Edits preview through shared config mutation helpers and the same renderer resolution path used by ordinary runtime rendering.
- The modeless editor window uses the same config mutation and preview path as drag-based editing so both interaction styles operate on the same session state, and post-menu hover recovery relies on explicit cursor refresh instead of rebuilding hover inside `WM_MOUSELEAVE`.

### Persistence and elevation flow

- Minimal saves diff live state against the loaded target INI text and preserve unrelated lines.
- Full exports start from the embedded template text.
- When the executable-side config file is not writable, the save path relaunches the executable through the maintained elevated helper route and hands off the write through a temporary file.
- Auto-start registration and configure-display writes use their package-owned elevated helper paths when the current process cannot write the target registry value or executable-side files directly.

## Resources And Build Graph

- `resources/SystemTelemetry.rc` is the single resource script for the manifest, dialogs, icons, embedded config, and embedded localization catalog.
- `resources/resource.h` owns the resource and control ids used by shell and dialog code.
- `CMakeLists.txt` is the single native build graph for the app, tests, benchmarks, resources, and the mixed-mode Gigabyte board-provider object library.
- The native app target links the shell, controller, config, telemetry, renderer, diagnostics, widget, and layout-edit subsystems into one Win32 executable.
- `src/telemetry/board/gigabyte/board_gigabyte_siv.cpp` builds as a CLR-enabled unit so it can bridge to the vendor .NET assemblies.
- The test build also produces `SystemTelemetryBenchmarks`, which exercises the layout-edit drag, layout-switch, and telemetry-refresh paths through the same runtime subsystems used by the app. Its supported benchmark names are held in an `enum_string`-backed selector, and each named benchmark owns its top-level command flow in a separate function.

## Source Dependency Graph

- `lint.cmd` writes the maintained DOT and GraphML views of non-vendored `src` module dependencies under `build\architecture\` before checking graph rules.
- Each graph node represents a source module, where a matching `.h` and `.cpp` pair share one node named by the extensionless path under `src`.
- DOT clusters group nodes by their containing source directory.
- GraphML nodes include `label` and `directory` data, and GraphML edges include `label` and `kind` data.
- A dependency from an including module to an included module is `public` when it appears in a header and `private` when it appears only in an implementation file.
- Files below package subdirectories are package-private implementation modules, so dependencies from a different top-level package into modules such as `widget/impl/*`, `telemetry/board/*`, or `dashboard_renderer/impl/*` fail the source dependency check.
- `src/util/` is the base layer, so util modules may depend on other util modules but must not depend on non-util project modules.
- `src/config/` is the second layer, so config modules may depend on other config modules and util modules but must not depend on non-config, non-util project modules.
- `src/telemetry/` is the third layer, so telemetry modules may depend on telemetry, config, and util modules but must not depend on higher-level project modules.
