# Telemetry Package

`src/telemetry/` owns live collection, fake collection, the telemetry runtime thread, telemetry snapshot contracts, snapshot dump-facing telemetry types, provider bridges, retained histories, the production metric catalog, FPS provider contracts, and the service IPC protocol.

## Responsibilities

- `TelemetryRuntime` owns steady-state collection, snapshot publishing callbacks, provider composition, and runtime target resolution for GPU, network, and storage.
- `telemetry/timing.h` owns the shared 250 ms refresh interval, 120-sample raw scalar retained-history window, 30-point compact throughput retained-history window, four-sample throughput live window, and derived throughput marker spacing used by telemetry, metrics, widgets, and live dashboard animation.
- Package-private collectors perform synchronous provider work behind `TelemetryRuntime`.
- Windows-native collection covers CPU, memory, network, storage, and clock data.
- Hardware providers extend collection with supported GPU and board telemetry paths.
- GPU telemetry resolves the configured GPU adapter selection name against unique non-software DXGI adapter candidates, maps the selected adapter identity through the pure GPU vendor-selection module, and creates one matching provider. Empty configuration selects the first candidate; changing the adapter at runtime shuts down the current provider and creates a provider for the new adapter.
- Board telemetry extracts baseboard registry strings, maps them through the pure board vendor-selection module, and creates one matching provider.
- Presented-FPS collection asks `CashDashService` over `\\.\pipe\CashDashService` first, then falls back to local ETW collection when the service is absent or unreachable.
- The service pipe protocol uses a generic request envelope with a stable request id and request name. The current service queries are `PresentedFpsSample` / `presented_fps_sample` and `BoardSensorsSample` / `board_sensors_sample`. The service accepts connected pipe clients on worker threads so a slow board-sensor request does not block presented-FPS samples or later client connections.
- The metric catalog adapts snapshots and metric definitions into widget-facing metric values, histories, drive rows, and formatted text.
- Fake-runtime support serves either the built-in synthetic snapshot or a reloadable dump-backed snapshot. The built-in synthetic source has a static mode for deterministic one-shot diagnostics and a live mode for the UI and benchmark harnesses; live synthetic throughput seeds and advances through `RetainedHistoryStore` so fake charts use the same four-sample smoothing and compact-history updates as real telemetry.
- Board telemetry keeps discovered provider sensor-name lists cached alongside live samples so layout-edit binding pickers stay populated across transient sample gaps.

## Subpackages

- `telemetry/board/` contains board-provider extraction, pure vendor selection, supported provider bridges, and unsupported-board fallback behavior.
- `telemetry/gpu/` contains GPU-provider extraction, pure vendor selection, supported provider bridges, and unsupported-GPU fallback behavior.
- `telemetry/fps/` contains package-private Windows ETW presented-FPS and service-client provider implementations, with `telemetry/fps/impl/` for provider-local helpers such as the GPU raw-counter map.
- `telemetry/impl/` contains collector submodules and system-info support.

## Boundaries

- `telemetry` may depend on `telemetry`, `config`, and `util`.
- It publishes runtime contracts such as `TelemetryRuntime`, `SystemSnapshot`, provider samples, and metric resolution for higher packages.
- It does not depend on renderer, widget, dashboard, diagnostics, display, layout-edit, or main.
- Provider .NET assembly reflection stays in CLR-enabled bridge translation units; native provider state stays out of CLR metadata.
- Native vendor providers keep provider-specific setup and method translation inside the provider module, including contracts such as ASUS ATKACPI `DSTS` device calls, Lenovo diagnostics-driver CPU temperature reads, Lenovo GameZone WMI fan calls, and vendor SDK bridges, then publish normal board-provider samples to the collector.
