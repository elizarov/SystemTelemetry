# System Telemetry Architecture

This document owns code structure, subsystem boundaries, major runtime flows, and build-graph shape.
See also: [docs/specifications.md](specifications.md) for normative product behavior, [docs/diagnostics.md](diagnostics.md) for diagnostics contracts, [docs/layout.md](layout.md) for config language, and [docs/build.md](build.md) for developer setup and commands.

## Top-Level Map

- `src/` contains the runtime application, configuration, telemetry, rendering, diagnostics, and layout-edit implementation.
- `src/widget/` contains the concrete widget draw and layout-state modules used by the renderer.
- `src/layout_edit_dialog/` contains the internal modules behind the modeless `Edit Configuration` window.
- `src/telemetry/` contains collector submodules for CPU, GPU, board, network, storage, and fake-runtime support.
- `resources/` contains the resource script, embedded config and localization files, dialog templates, manifest, and image assets.
- `tests/` contains unit tests for config, layout resolution, retained-history behavior, and the native benchmark host.
- `tools/` contains shared formatting, lint, tidy, and profiling helper scripts.

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
- Widget modules own widget-local preferred-size logic, draw behavior, and layout-edit artifact registration.
- `DashboardMetricSource` adapts `SystemSnapshot` into widget-facing values, histories, drive rows, and formatted text while caching per-frame derived results.

### Layout editing

- `LayoutEditController` owns hover state, active drags, hit-testing, capture, cursor choice, and drag-session flow.
- Layout-edit parameter metadata and helpers centralize editable target identity, config-path mapping, clamps, tooltip formatting, and preview application.
- `LayoutEditDialog` owns the modeless editor window, config-tree selection, right-pane editing, and preview or revert flow, with focused helper modules under `src/layout_edit_dialog/`.
- The renderer exposes the resolved guide and anchor geometry used both by live interaction and by diagnostics screenshot validation.

### Diagnostics

- The diagnostics subsystem parses diagnostics CLI switches, manages headless `/exit` runs, owns requested output exports, and creates the top-level trace session when trace is enabled.
- Snapshot dumps and fake-runtime imports share the same dump serializer and parser.
- Diagnostics screenshot export uses the same renderer scene as the live window path instead of a separate rendering implementation.

## Runtime Flows

### Startup and config flow

- `app_main` initializes process-wide shell settings, parses command-line options, and chooses either the normal UI path or the headless diagnostics path.
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

### Diagnostics flow

- The diagnostics path optionally reloads config through the same live reload logic used by the dashboard.
- Requested outputs write trace, dump, screenshot, minimal-config, or full-config artifacts using the same runtime state the live app would use.
- `/exit` performs one update-and-export pass and exits without joining the normal single-instance UI lifetime.

### Layout-edit flow

- The shell forwards pointer events into the layout-edit controller, which resolves actionable targets from renderer-provided guide and anchor data.
- Edits preview through shared config mutation helpers and the same renderer resolution path used by ordinary runtime rendering.
- The modeless editor window uses the same config mutation and preview path as drag-based editing so both interaction styles operate on the same session state.

### Persistence and elevation flow

- Minimal saves diff live state against the loaded target INI text and preserve unrelated lines.
- Full exports start from the embedded template text.
- When the executable-side config file is not writable, the save path relaunches the executable through the maintained elevated helper route and hands off the write through a temporary file.

## Resources And Build Graph

- `resources/SystemTelemetry.rc` is the single resource script for the manifest, dialogs, icons, embedded config, and embedded localization catalog.
- `resources/resource.h` owns the resource and control ids used by shell and dialog code.
- `CMakeLists.txt` is the single native build graph for the app, tests, benchmarks, resources, and the mixed-mode Gigabyte board-provider object library.
- The native app target links the shell, controller, config, telemetry, renderer, diagnostics, widget, and layout-edit subsystems into one Win32 executable.
- `src/board_gigabyte_siv.cpp` builds as a CLR-enabled unit so it can bridge to the vendor .NET assemblies.
- The test build also produces `SystemTelemetryBenchmarks`, which exercises the real layout-edit and telemetry-refresh paths through the same runtime subsystems used by the app.
