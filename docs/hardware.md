# Hardware Support

This document owns supported hardware-provider details, provider runtime requirements, provider-specific telemetry behavior, and provider troubleshooting.
See also: [docs/specifications.md](specifications.md) for general product behavior, [docs/build.md](build.md) for build and developer setup, [docs/diagnostics.md](diagnostics.md) for diagnostics commands, and [docs/architecture/telemetry.md](architecture/telemetry.md) for telemetry package structure.

## Provider Model

- CaseDash uses Windows-native telemetry for generic CPU, memory, network, storage, drive activity, clock, and presented-FPS data.
- Hardware providers add hardware-specific GPU and board metrics when the matching driver, SDK, or utility is installed.
- Unsupported or unavailable hardware providers do not prevent the dashboard from running; their provider-owned values render as unavailable.
- GPU telemetry selects the supported hardware provider from the primary non-software DXGI adapter identity.
- Board telemetry selects the supported hardware provider from the baseboard manufacturer.
- Trace output can include `gpu_vendor:*` and `board_vendor:*` selection details, provider-specific diagnostics, and unsupported-provider fallback markers.
- Layout metric references are the source of truth for requested logical board metrics. The `[board]` mapping connects logical names to provider-specific sensor names.
- Empty CPU and system board bindings use first-use auto-detection from the active provider's sensor names; otherwise, bound board metrics resolve when the mapped sensor exists.

## Adding Hardware Support

- Provider selection stays split into three steps: extract vendor info, map vendor info to the vendor enum, and create the matching provider.
- GPU extraction reads the primary non-software DXGI adapter identity into `GpuVendorInfo`, including the PCI vendor id and adapter name.
- Board extraction reads baseboard registry strings into `BoardVendorInfo`, including manufacturer and product.
- Vendor mapping lives in `src/telemetry/gpu/gpu_vendor_selection.*` and `src/telemetry/board/board_vendor_selection.*`; provider factories instantiate modules only after that mapping returns a supported enum value.
- Each added GPU or board hardware module extends `tests/hardware_vendor_selection_tests.cpp` with a known-machine fixture from hardware that has actually run CaseDash. Record the GPU vendor id and adapter string for GPU support, the board manufacturer and product strings for board support, and the expected vendor enum values.
- Runtime requirements, telemetry behavior, trace markers, and troubleshooting for supported providers stay in this file. README and website hardware sections remain concise summaries that point here for details.

## Presented FPS

- Presented FPS comes from Windows present-event telemetry and process selection, with the machine-wide CaseDash service used when installed.
- The dashboard asks the service for the `presented_fps_sample` request and falls back to local ETW collection when the service is absent or unreachable.
- Local ETW collection may require elevation or membership in the local `Performance Log Users` group.
- Process selection prefers a presenting process with dominant GPU Engine 3D usage over background presenters and keeps the current presenter through brief count ties or near-ties.
- When a dominant 3D application is visible but matching present events are not, `gpu.fps` reports unavailable for that application instead of showing another process's FPS.
- The FPS sample includes the selected presenter's cleaned process name without path or extension when available.
- If Windows denies process-name or ETW access, the dashboard marks the affected FPS display with the warning-colored `!admin` indicator when a value or permission state can still be shown.

## AMD

- Supported hardware family: Radeon GPU telemetry.
- Runtime dependency: AMD Software: Adrenalin Edition with ADLX available.
- Metrics include provider-supplied GPU telemetry such as load, dedicated memory, temperature, clock, fan speed, and native FPS when the driver exposes them.
- Presented FPS remains the default `gpu.fps` source when available. Native FPS is used only as a fallback when presented-FPS collection is unavailable because Windows denies local ETW access; in that fallback, the metric keeps the native FPS value and annotates the row with warning-colored `!admin`.
- Trace output can include `amd_adlx:*` provider details and `unsupported_gpu` fallback markers.

Troubleshooting:

1. Install or update AMD Software: Adrenalin Edition.
2. Confirm `amdadlx64.dll` is present in `C:\Windows\System32`.
3. Run the matching dump or trace validation flow from [docs/diagnostics.md](diagnostics.md).
4. Inspect the exported dump and trace outputs for the provider state on that machine.

## NVIDIA

- Supported hardware family: NVIDIA GPU telemetry.
- Runtime dependency: NVIDIA display driver with NVML available.
- Metrics include provider-supplied GPU telemetry such as load, dedicated memory, temperature, clock, and fan speed.
- Presented FPS is the smoothed rolling presented-FPS rate from Windows DXGI, D3D9, or fallback DxgKrnl ETW present events because NVML has no native game-FPS metric.
- Trace output can include `nvidia_nvml:*` provider details and `unsupported_gpu` fallback markers.

Troubleshooting:

1. Install or update the NVIDIA display driver.
2. Confirm `nvml.dll` is present in `C:\Windows\System32` or the NVIDIA NVSMI install directory.
3. Run the matching dump or trace validation flow from [docs/diagnostics.md](diagnostics.md).
4. Inspect the exported dump and trace outputs for the provider state on that machine.

## MSI

- Supported hardware family: MSI board telemetry.
- Runtime dependency: MSI Center SDK.
- Metrics include board temperature and fan telemetry exposed by the local SDK.
- Trace output can include `msi_center:*` provider details and `unsupported_board` fallback markers.

Troubleshooting:

1. Install MSI Center with the SDK/runtime components that expose board sensors.
2. Run the matching trace plus dump validation flow from [docs/diagnostics.md](diagnostics.md).
3. Inspect the dump for `board.*` values and the trace for `msi_center:*` diagnostics.

## Gigabyte

- Supported hardware family: Gigabyte board telemetry.
- Runtime dependency: Gigabyte SIV.
- Metrics include board temperature and fan telemetry exposed by the installed SIV assemblies.
- Assembly loading can require the SIV install directory as the process current directory; CaseDash restores the original launch working directory afterward.
- Trace output can include `gigabyte_siv:*` provider details and `unsupported_board` fallback markers.

Troubleshooting:

1. Install Gigabyte SIV.
2. Run the matching trace plus dump validation flow from [docs/diagnostics.md](diagnostics.md).
3. Inspect the dump for `board.*` values and the trace for `gigabyte_siv:*` diagnostics.
