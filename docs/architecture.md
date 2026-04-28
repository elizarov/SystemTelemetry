# System Telemetry Architecture

This document owns code structure, subsystem boundaries, major runtime flows, and build-graph shape.
See also: [docs/specifications.md](specifications.md) for normative product behavior, [docs/diagnostics.md](diagnostics.md) for diagnostics contracts, [docs/layout.md](layout.md) for config language, and [docs/build.md](build.md) for developer setup and commands.

## Top-Level Map

- `src/` contains the runtime application, configuration, telemetry, rendering, diagnostics, and layout-edit implementation.
- `src/config/` contains the config model, parser, resolver, writer, schema metadata, config-facing enum and DTO contracts, and injected metric-catalog view.
- `src/display/` contains monitor enumeration, placement, configure-display, wallpaper application helpers, and display-owned constants.
- `src/diagnostics/` contains diagnostics session and headless-run orchestration, command-line option parsing, default diagnostics output filenames, snapshot dump I/O, and diagnostics-owned support modules.
- `src/main/` contains the application entry point, runtime config I/O, login auto-start registry updates, elevation handoff, and main-process constants.
- `src/widget/widget.*` owns the widget interface plus the enum-backed and special widget factories, `src/widget/widget_host.h` owns the widget-facing host boundary, `src/widget/card_chrome_layout.*` owns shared card-chrome layout geometry, `src/widget/layout_edit_types.h` owns widget-facing edit-artifact DTO contracts, and `src/widget/impl/` contains the concrete widget draw and layout-state modules used by the dashboard renderer.
- `src/layout_model/` contains renderer-safe shared layout-edit model contracts and behavior, including edit-target identity, artifact matching, hit priority, read-only parameter metadata, guide-weight preview helpers, and dashboard overlay state.
- `src/util/` contains pure shared utilities for paths, command-line text, string trimming, splitting, case folding, whitespace normalization, enum string conversion, UTF-8 conversion, embedded resource loading, localization catalog access, numeric safety, DPI scale conversion, and trace emission.
- `src/dashboard/` contains the dashboard application, controller, shell UI, dashboard command and timer constants, menu types, and shared layout-edit overlay state.
- `src/renderer/` owns render-space contract types, the `Renderer` drawing interface, renderer style resources, render-target lifecycle, and the Direct2D/DirectWrite/WIC implementation under `src/renderer/impl/`. It does not own dashboard drawing modes, overlay policy, or trace emission.
- `src/dashboard_renderer/dashboard_renderer.*` owns dashboard scene traversal, renderer style input selection, drawing-mode state, widget-host services, and layout-edit active-region collection. It implements `WidgetHost`, owns a `Renderer` instance, and keeps graphics-backend details encapsulated in `src/renderer/`.
- `src/layout_edit/` contains runtime layout-edit interaction, active-region hit testing, drag flow, controller-host integration, tooltip payload interpretation and text formatting, edit-tree construction, config-field mutation helpers, guide and reorder config helpers, diagnostics active-region trace formatting, and trace-session modules; `src/layout_edit/impl/` contains package-private layout-edit implementation modules such as the snap solver.
- `src/layout_edit_dialog/layout_edit_dialog.*` owns the modeless `Edit Configuration` window boundary, and `src/layout_edit_dialog/impl/` contains its internal dialog modules.
- `src/telemetry/telemetry.*` owns the telemetry collector boundary, `src/telemetry/metrics.*` owns the single production metric catalog and adapts snapshots and metric definitions into widget-facing metric values, `src/telemetry/metric_types.h` owns telemetry snapshot enums, `src/telemetry/board/` and `src/telemetry/gpu/` contain vendor-provider bridges, and `src/telemetry/impl/` contains collector submodules plus system-info support for CPU, GPU, board, network, storage, and fake-runtime support.
- `resources/` contains the resource script, embedded config and localization files, dialog templates, manifest, and image assets.
- `tests/` contains unit tests for config, layout resolution, retained-history behavior, and the native benchmark host.
- `tools/` contains shared formatting, lint, tidy, profiling, and source dependency graph helper scripts.

## Layered Core

- The core project layers are ordered `util` -> `config` -> `renderer` and `telemetry` -> `widget` -> `layout_model` -> application-facing packages such as dashboard, dashboard_renderer, diagnostics, display, layout-edit, layout-edit dialog, and main.
- Dependencies flow downward only. A higher layer may include lower-layer contracts, but a lower layer must not include or call into a higher layer.
- `src/util/` is the base layer. It contains domain-neutral helpers for text, paths, resources, enum strings, UTF-8 conversion, localization catalog access, numeric safety, DPI scale conversion, and trace emission. Util modules may depend on other util modules, but must not depend on config, telemetry, rendering, UI, diagnostics, or application packages.
- `src/config/` is the second layer. It owns the persisted config model, parser, writer, resolver, schema metadata, and config-facing contract types such as widget class, metric display style, and telemetry settings DTOs. Config modules may depend only on config and util modules.
- Config must not duplicate runtime catalogs or reach upward to validate runtime concepts. When config parsing needs runtime knowledge, it uses config-owned injection contracts such as `ConfigMetricCatalog`; production code supplies the telemetry-backed implementation from above.
- `src/telemetry/` is the third layer. It owns live collection, fake collection, snapshot and dump-facing telemetry types, provider bridges, retained history, and the single production metric catalog. Telemetry modules may depend on telemetry, config, and util modules, but must not depend on renderer, widget, dashboard, diagnostics, display, layout-edit, or main modules.
- Telemetry is allowed to consume config contracts such as telemetry settings and metric display style, and it publishes runtime contracts such as `TelemetryCollector`, `SystemSnapshot`, provider samples, and metric resolution for higher packages.
- `src/renderer/` owns render-space DTOs, renderer style DTOs, the D2D-free `Renderer` interface, and the only Direct2D, DirectWrite, WIC, and WRL implementation modules. Renderer modules may depend only on renderer, config, and util modules.
- `src/widget/` is the widget layer. It owns widget contracts, widget-local layout and drawing behavior, widget-facing layout-edit DTO contracts, and the D2D-free `WidgetHost` interface. Widget modules may depend on widget, renderer, telemetry, config, and util modules, but must not depend on dashboard, diagnostics, display, layout-edit, main, Direct2D, DirectWrite, WIC, or WRL modules.
- `src/layout_model/` is the shared layout-edit model layer. It may depend on layout_model, widget, renderer, and config modules, but must not depend on dashboard, dashboard_renderer, diagnostics, display, layout-edit, layout-edit dialog, main, telemetry, util, Direct2D, DirectWrite, WIC, or WRL modules.
- `src/dashboard_renderer/` is the dashboard scene and layout-rendering layer. It may depend on dashboard_renderer, layout_model, widget, renderer, telemetry, config, and util modules, but must not depend on dashboard, diagnostics, display, layout-edit, layout-edit dialog, main, Direct2D, DirectWrite, WIC, or WRL modules.
- `src/layout_edit/` is the runtime layout-edit interaction layer. It may depend on layout_edit, config, layout_model, util, and widget modules, but must not depend on dashboard, dashboard_renderer, diagnostics, display, layout-edit dialog, main, renderer, telemetry, Direct2D, DirectWrite, WIC, or WRL modules.
- `src/layout_edit_dialog/` is the modeless layout-editor dialog layer. It may depend on layout_edit_dialog, config, layout_edit, layout_model, telemetry, util, and widget modules, but must not depend on dashboard, dashboard_renderer, diagnostics, display, main, renderer, Direct2D, DirectWrite, WIC, or WRL modules.
- Cross-layer shared types belong in the lowest layer that semantically owns them. Move config-language DTOs to config, runtime telemetry DTOs to telemetry, and domain-neutral helpers to util; do not copy catalogs or enums across layers to avoid a dependency violation.
- `lint.cmd` enforces the util, config, renderer, telemetry, widget, layout-model, dashboard-renderer, layout-edit, layout-edit-dialog, and renderer-only D2D rules through the source dependency graph before reporting success.

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

- `Renderer` owns renderer style resources, icon loading, text measurement, live HWND rendering, shared offscreen bitmap rendering for screenshot export and validation priming, and primitive drawing such as text, rectangles, rounded rectangles, ellipses, lines, arcs, polylines, filled paths, clips, and translations. It stays independent of application trace sinks and dashboard-specific drawing modes.
- `DashboardRenderer` owns renderer style input selection, resolved scene traversal, drawing-mode state, widget-host services, and production of the `LayoutEditActiveRegions` snapshot used by layout-edit interaction.
- `DashboardLayoutResolver` owns static layout resolution plus resolved dashboard, card, widget, guide, anchor, and dynamic edit-artifact geometry inside the dashboard-renderer package. It implements the widget-facing edit-artifact registrar because it owns the registered artifact storage.
- `DashboardLayoutEditOverlayRenderer` owns layout-edit overlay presentation inside the dashboard-renderer package, including selected and hovered highlights, layout and widget guides, gap anchors, size similarity indicator policy, dotted outlines, and dragged container-child replay.
- Shared renderer-owned render-space contract types isolate the rest of the codebase from low-level Direct2D and DirectWrite structs.
- `WidgetHost` is the widget-facing host interface consumed by widgets; it exposes config, render mode, a dedicated edit-artifact registrar, metric lookup, metric-list reorder state, and a `Renderer` reference. Widgets call drawing and text operations through that renderer reference.
- Widget draw modules refer to colors by render color id; the renderer keeps the resolved RGBA palette private and maps ids to colors internally.
- Widget modules own widget-local preferred-size logic, draw behavior, layout-edit artifact registration, and shared card-chrome geometry used both by layout resolution and by the special card-chrome widget.
- `MetricSource` adapts `SystemSnapshot` into widget-facing values, histories, drive rows, and formatted text while caching per-frame derived results.

### Layout editing

- `LayoutEditController` owns hover state, active drags, capture, cursor choice, and drag-session flow. Layout-edit hit testing and snap-candidate discovery operate on renderer-produced `LayoutEditActiveRegions` snapshots inside the layout-edit package.
- Layout-model helpers own renderer-safe edit-artifact matching, focus and selection resolution, anchor subject extraction, and edit-artifact ordering policy. Layout-edit modules own tooltip-payload interpretation, tooltip formatting, edit-tree construction, current-value lookup, and preview application.
- Widget layout-node parameter edits use `LayoutNodeFieldEditKey` plus layout-edit descriptors so tree labels, editor kind, title, hint, tooltip description, trace identity, value format, and preview routing are declared in one place.
- Layout-node config mutations resolve `{editCardId,nodePath}` through shared layout-edit helpers and mirror dashboard-layout edits into the active named layout when the edit targets the live dashboard layout.
- `LayoutEditDialog` owns the modeless editor window, config-tree selection, right-pane editing, and preview or revert flow, with focused helper modules under `src/layout_edit_dialog/impl/`.
- The dashboard renderer exposes copied active-region geometry as a `LayoutEditActiveRegions` value object used by live interaction and diagnostics screenshot validation; layout-edit modules interpret that snapshot for hit testing, snap discovery, tooltip targets, and active-region trace output.

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

- The shell forwards pointer events into the layout-edit controller, which resolves actionable targets from renderer-provided `LayoutEditActiveRegions` snapshots.
- Package-private layout-edit interaction helpers live under `src/layout_edit/impl/` when they have no incoming production dependencies from outside `src/layout_edit/`.
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
- The test build also produces `SystemTelemetryBenchmarks`, which exercises the layout-edit drag, layout-switch, layout-edit mouse-hover, and telemetry-refresh paths through the same runtime subsystems used by the app. Its supported benchmark names are held in an `enum_string`-backed selector, and each named benchmark owns its top-level command flow in a separate function.

## Source Dependency Graph

- `lint.cmd` writes the maintained DOT and GraphML views of non-vendored `src` module dependencies under `build\architecture\` before checking graph rules.
- Each graph node represents a source module, where a matching `.h` and `.cpp` pair share one node named by the extensionless path under `src`.
- Graph generation counts physical source lines in each non-vendored `.h` and `.cpp` file, annotates each node with header, implementation, and total LOC, and prints LOC totals for each top-level `src` package plus an overall total. It also condenses the real source package dependency graph into strongly connected components, prints those components in topological order with their combined LOC, package list, and lower-level package dependencies after `->`, and prints every non-vendored source file above 1,000 LOC, ordered largest first.
- The graph includes a synthetic `d2d` package for Direct2D, DirectWrite, WIC, and WRL includes so every package except `renderer` rejects accidental graphics-stack coupling.
- DOT clusters group nodes by their containing source directory, and the optional SVG graph is rendered from the DOT graph with Graphviz `dot` when `tools\source_dependency_graph.py` runs without `--skip-svg`.
- GraphML nodes include `label`, `directory`, `header_loc`, `cpp_loc`, `total_loc`, and `loc_annotation` data, and GraphML edges include `label` and `kind` data.
- A dependency from an including module to an included module is `public` when it appears in a header and `private` when it appears only in an implementation file.
- Files below package subdirectories are package-private implementation modules, so dependencies from a different top-level package into modules such as `widget/impl/*`, `telemetry/board/*`, or `dashboard_renderer/impl/*` fail the source dependency check.
- `src/util/` is the base layer, so util modules may depend on other util modules but must not depend on non-util project modules.
- `src/config/` is the second layer, so config modules may depend on other config modules and util modules but must not depend on non-config, non-util project modules.
- `src/telemetry/` is the third layer, so telemetry modules may depend on telemetry, config, and util modules but must not depend on higher-level project modules.
- `src/renderer/` is the graphics boundary, so renderer modules may depend on renderer, config, util, and the synthetic `d2d` package only.
- `src/widget/` is the widget layer, so widget modules may depend on widget, renderer, telemetry, config, and util modules but must not depend on higher-level project modules or the synthetic `d2d` package.
- `src/layout_model/` is the shared layout-edit model layer, so layout-model modules may depend on layout_model, config, renderer, and widget modules but must not depend on other project modules or the synthetic `d2d` package.
- `src/dashboard_renderer/` is the dashboard scene and layout-rendering layer, so dashboard-renderer modules may depend on dashboard_renderer, config, layout_model, renderer, telemetry, util, and widget modules but must not depend on higher-level project modules or the synthetic `d2d` package.
- `src/layout_edit/` is the runtime layout-edit interaction layer, so layout-edit modules may depend on layout_edit, config, layout_model, util, and widget modules but must not depend on other project modules or the synthetic `d2d` package.
- `src/layout_edit_dialog/` is the modeless layout-editor dialog layer, so layout-edit-dialog modules may depend on layout_edit_dialog, config, layout_edit, layout_model, telemetry, util, and widget modules but must not depend on other project modules or the synthetic `d2d` package.
