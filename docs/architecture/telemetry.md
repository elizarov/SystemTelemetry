# Telemetry Package

`src/telemetry/` owns live collection, fake collection, the telemetry runtime thread, snapshot and dump-facing telemetry types, provider bridges, retained histories, the production metric catalog, FPS provider contracts, and the service IPC protocol.

## Responsibilities

- `TelemetryRuntime` owns steady-state collection, snapshot publishing callbacks, provider composition, and runtime target resolution for network and storage.
- `telemetry/timing.h` owns the shared 250 ms refresh interval, 120-sample raw scalar retained-history window, 30-point compact throughput retained-history window, four-sample throughput live window, and derived throughput marker spacing used by telemetry, metrics, widgets, and live dashboard animation.
- Package-private collectors perform synchronous provider work behind `TelemetryRuntime`.
- Windows-native collection covers CPU, memory, network, storage, and clock data.
- Hardware providers extend collection with supported GPU and board telemetry paths.
- GPU telemetry selects one provider from the primary non-software DXGI adapter identity; unsupported GPU providers expose only presented FPS.
- Presented-FPS collection asks `CashDashService` over `\\.\pipe\CashDashService` first, then falls back to local ETW collection when the service is absent or unreachable.
- The FPS pipe protocol uses a generic request envelope with a stable request id and request name. The current FPS query is `PresentedFpsSample` / `presented_fps_sample`.
- The metric catalog adapts snapshots and metric definitions into widget-facing metric values, histories, drive rows, and formatted text.
- Fake-runtime support serves either the built-in synthetic snapshot or a reloadable dump-backed snapshot.
- Board telemetry keeps discovered provider sensor-name lists cached alongside live samples so layout-edit binding pickers stay populated across transient sample gaps.

## Subpackages

- `telemetry/board/` contains board-provider selection plus supported provider bridges.
- `telemetry/gpu/` contains GPU provider bridges and unsupported-GPU fallback behavior.
- `telemetry/fps/` contains package-private Windows ETW presented-FPS and service-client provider implementations, with `telemetry/fps/impl/` for provider-local helpers such as the GPU raw-counter map.
- `telemetry/impl/` contains collector submodules and system-info support.

## Boundaries

- `telemetry` may depend on `telemetry`, `config`, and `util`.
- It publishes runtime contracts such as `TelemetryRuntime`, `SystemSnapshot`, provider samples, and metric resolution for higher packages.
- It does not depend on renderer, widget, dashboard, diagnostics, display, layout-edit, or main.
- Provider .NET assembly reflection stays in CLR-enabled bridge translation units; native provider state stays out of CLR metadata.
