# Hardware Dependency Interface

This document owns the hardware dependency interface, or HDI, in the telemetry package. It describes the implemented test boundary and the remaining migration target for hardware-provider dependencies. `docs/hardware.md` remains the source of truth for supported hardware providers and provider troubleshooting, and `docs/architecture/telemetry.md` remains the source of truth for current telemetry package boundaries.

## Goal

HDI makes hardware-provider integration testable on any developer or CI machine. Tests can create the same provider selection, initialization, fallback, and sample paths that run on real machines by injecting deterministic hardware-facing results instead of calling the current machine's vendor SDKs, Windows hardware APIs, privileged service paths, or board utilities.

The target test surface covers GPU telemetry, board telemetry, and presented FPS because those paths combine hardware discovery, provider selection, vendor runtime availability, permissions, and provider-specific sampling. Windows-native CPU, memory, network, storage, drive activity, and clock telemetry remain outside this proposal because their collection paths already run on ordinary test machines without matching vendor hardware.

## Design Principles

- HDI wraps calls whose results depend on installed hardware, installed provider runtimes, hardware permissions, or the CaseDash service.
- HDI does not replace `GpuVendorTelemetryProvider`, `BoardVendorTelemetryProvider`, `FpsTelemetryProvider`, `TelemetryRuntime`, snapshots, retained histories, or the metric catalog.
- Production HDI implementations forward directly to the external API they represent. They do not interpret provider behavior, normalize values, apply fallback policy, or choose diagnostics text beyond raw call tracing.
- Provider modules keep provider-specific interpretation, capability checks, fallback decisions, metric mapping, and user-facing diagnostics.
- Tests supply mock HDI implementations that return realistic discovery and sample sequences for known hardware configurations.
- Interface headers stay narrow and declarative. Production loading, forwarding, tracing, and native resource lifetime live in `.cpp` files.
- Mock-backed runs fail when provider code asks for an HDI surface the test fixture did not supply. Missing mocks are test setup errors because each fixture declares the hardware path it expects to exercise.

## Scope

HDI covers the machine-dependent boundaries needed to exercise hardware-provider behavior end to end:

- GPU adapter discovery and matching inputs, including DXGI adapter enumeration, DXGI adapter descriptions and LUIDs, PCI identity lookup through D3DKMT, GPU Engine and GPU Adapter Memory counters used by fallback GPU metrics, and adapter activity inputs used by presented-FPS process selection.
- AMD ADLX calls used by the AMD GPU provider, including runtime initialization, GPU enumeration, GPU identity, supported metric checks, total VRAM, and metric sampling.
- Intel Level Zero Sysman calls used by the Intel GPU provider, including loader availability, Sysman and core enumeration, device properties, engine, temperature, fan, frequency, memory components, and WDDM node clock fallback.
- NVIDIA NVML and NVAPI calls used by the NVIDIA GPU provider, including dynamic library loading, entrypoint lookup, initialization, GPU enumeration, PCI identity, memory, temperature, fan RPM, and graphics clock sampling.
- Board vendor discovery inputs, including baseboard registry strings.
- ASUS ATKACPI calls used by the ASUS board provider, including device open and `DSTS` IOCTL requests.
- MSI Center SDK registry discovery and managed bridge calls used by the MSI board provider.
- Gigabyte SIV registry discovery, current-directory-sensitive managed bridge setup, and managed bridge calls used by the Gigabyte board provider.
- Lenovo Diagnostics Driver discovery, driver service wrapper loading, service-control interactions, direct CPU-temperature reads, GameZone WMI fan calls, and service-backed board-sensor queries.
- Presented-FPS service client and local ETW collection boundaries, including pipe request and response behavior, ETW access failures, selected-adapter filtering inputs, and permission-gated states.

HDI does not cover provider-independent formatting, metric lookup, config parsing, layout resolution, rendering, or generic retained-history logic. Those remain tested through existing unit tests and fake-runtime paths.

## Interface Shape

Each HDI represents one cohesive external boundary with one lifetime model. A boundary can be a dynamic native library, a COM or WMI surface, a managed bridge, a Win32 device handle, a service pipe, or a hardware-discovery source. Windows-native GPU and board discovery are wrapped because provider selection needs deterministic DXGI adapter, D3DKMT PCI identity, and baseboard identity inputs in tests.

Production naming uses `hdi_<surface>.h` and `hdi_<surface>.cpp`; use `hdi`, not `hid`. Shared hardware-discovery HDIs live under `src/telemetry/impl/`. Vendor-specific HDIs live beside the provider that owns them, such as `src/telemetry/gpu/nvidia/impl/` or `src/telemetry/board/asus/impl/`, when an `impl` subdirectory is needed to keep provider headers small.

Each production HDI exposes only the operations CaseDash uses, not the full vendor SDK. Dynamic-library HDIs provide a `Load` or `InitializeLoader` step that resolves required and optional entrypoints and returns raw availability status. Later methods forward to the loaded entrypoints and return the raw result code plus output values. Optional entrypoints are represented explicitly so provider code can keep the current fallback behavior.

HDI headers avoid depending on large vendor SDK headers when practical. Prefer opaque handle aliases and small POD mirrors for the fields CaseDash consumes. If an existing vendored header is already required for a provider, keep that dependency contained to the provider-specific HDI header and do not leak it into shared telemetry headers.

Factories create production HDIs from `Trace&` and provider options. `CreateTelemetryRuntime` accepts an optional `HardwareDependencyInjection` argument used by tests to install mock factories. The runtime borrows the injected `HdiFactory`; the factory and the mock state it returns must outlive the runtime. Normal runs omit that argument and use production factories. Provider factories receive the dependencies they need instead of constructing hardware libraries internally.

Fixture-backed factories are test-only. Diagnostics commands do not install mock HDIs or run hardware replay workflows through production binaries.

Presented FPS uses a separate HDI factory because its dependencies span service IPC, ETW collection, selected-adapter process selection, and GPU adapter activity rather than a single GPU vendor provider.

Managed board-provider seams for MSI Center and Gigabyte SIV sit at managed C++ bridge interfaces that can be implemented by production bridge code or by managed test mocks. The seam can stay above reflection details when the provider still owns parsing and decision-making behavior.

Large object-style SDKs, such as ADLX, may group closely related calls case by case. Grouping must not hide parsing, fallback decisions, capability interpretation, device matching, or metric mapping from provider tests.

## Tracing

Production HDI implementations write regular abbreviated trace summaries through the provider's existing trace prefix when that prefix is enabled. Summary trace lines name the operation phase, raw result code or Win32 status when relevant, and the small set of output fields needed to diagnose in-field provider behavior.

Existing ad hoc raw-call traces move into HDI implementations as summaries. Provider-level traces remain in provider modules when they describe CaseDash decisions, such as selected device rank, fallback source, capability summary, provider availability, requested board sensors, or user-facing diagnostics.

Sample-loop tracing stays compact. High-frequency provider samples use one concise line per logical external call or a grouped line per sample step, depending on the provider. The trace policy should preserve diagnostic coverage without making ordinary `/trace` hardware validation too noisy to inspect.

Verbose external-call capture is separate test-support tooling for building fixtures. It can record every hardware-facing call and result needed to author mocks, but that capture instrumentation does not live in the production telemetry path. Production keeps only regular abbreviated tracing for in-field debugging.

## Test Model

`tests/hdi/` owns the shared mock infrastructure and hardware-configuration tests. Fixtures are C++ builders so they stay type-checked with provider contracts. The fixture data represents external call results, not final snapshots, so tests exercise provider selection, initialization, parsing, fallback, metric mapping, and permission propagation.

Each hardware fixture includes:

- discovery inputs, such as DXGI adapters, PCI identity, baseboard manufacturer and product, registry runtime locations, service availability, and permission outcomes;
- provider initialization results, such as loaded libraries, missing optional entrypoints, component counts, matched device handles, supported metrics, and selected board sensors;
- one or more sample sequences, including available values, unavailable values, permission-gated values, sleeping GPU states, transient failures, and recovery samples;
- expected provider names, provider diagnostics, resolved metric values, unavailable metric states, warning indicators, and other externally observable provider outcomes.

Mocks are strict. They validate which methods are called, how many times they are called, and which calls are intentionally absent for a hardware path. This catches accidental provider work, repeated runtime loads, and performance regressions that snapshot-only assertions can miss.

Fixtures mirror machines that already have performance records:

- `msi-z690-carbon-wifi-rtx3080` covers the implemented shared discovery, GPU fallback performance-counter, NVIDIA NVML/NVAPI, and MSI Center HDIs through `CreateTelemetryRuntime` and one collected snapshot.
- `asus-gu603vi-intel-uhd-rtx4070` for hybrid Intel plus NVIDIA GPU selection, sleeping dGPU behavior, and ASUS board-backed fan fallback.
- `gigabyte-x570i-amd-rx6800` for AMD ADLX GPU telemetry and Gigabyte SIV board telemetry.
- `lenovo-lnvnb161216-intel-uhd-gtx1650ti` for Intel integrated GPU behavior, NVIDIA dGPU behavior, Lenovo service-backed board sampling, and permission-gated paths.

Tests assert through `CreateTelemetryRuntime` and the normal telemetry collection path where practical. Direct provider tests can cover narrow parsing and fallback cases, but the primary value comes from a full hardware-provider path running on any machine.

HDI tests run provider behavior in synchronous mode. Asynchronous providers, such as Lenovo direct refresh or service-backed samples, expose deterministic synchronous test hooks through the injected dependencies instead of sleeping or waiting on background work.

## Rollout State

The implemented vertical slice proves the seam through the MSI Z690 plus RTX 3080 fixture:

- `HardwareDependencyInjection` and `HdiFactory` are package-private test seams reached from `CreateTelemetryRuntime`.
- GPU adapter discovery, GPU fallback performance counters, and board vendor discovery are behind shared HDIs under `src/telemetry/impl/`.
- NVIDIA NVML and NVAPI are behind provider-local HDIs under `src/telemetry/gpu/nvidia/impl/`.
- MSI Center SDK discovery and managed bridge capture are behind a provider-local HDI under `src/telemetry/board/msi/impl/`.
- `tests/hdi/msi_z690_rtx3080_hdi_tests.cpp` asserts runtime creation, hardware discovery, provider initialization, one telemetry snapshot, and strict mock call counts.

Each migration removes raw external-call tracing from the provider module as the equivalent HDI tracing lands. The public hardware behavior and diagnostics contracts in `docs/hardware.md` should not change unless the implementation intentionally changes provider behavior.

## Remaining Work

- Decide how verbose fixture-capture tooling is built, invoked, and kept separate from production binaries.
- Migrate AMD ADLX, Intel Level Zero Sysman, ASUS ATKACPI, Gigabyte SIV, Lenovo diagnostics and service-backed board sampling, and presented FPS.
