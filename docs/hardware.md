# Hardware Support

This document owns supported hardware-provider details, provider runtime requirements, provider-specific telemetry behavior, and provider troubleshooting.
See also: [docs/specifications.md](specifications.md) for general product behavior, [docs/build.md](build.md) for build and developer setup, [docs/diagnostics.md](diagnostics.md) for diagnostics commands, and [docs/architecture/telemetry.md](architecture/telemetry.md) for telemetry package structure.

## Support At A Glance

- [AMD](#amd) - Radeon GPU telemetry through AMD ADLX.
- [Intel](#intel) - Intel GPU telemetry through Level Zero Sysman.
- [NVIDIA](#nvidia) - NVIDIA GPU telemetry through NVML.
- [Presented FPS](#presented-fps) - active presenting-application FPS through Windows present-event telemetry and the CaseDash service.
- [ASUS](#asus) - board CPU temperature and fan telemetry through Armoury Crate or ASUS System Control Interface ATKACPI.
- [MSI](#msi) - board temperature and fan telemetry through MSI Center SDK.
- [Gigabyte](#gigabyte) - board temperature and fan telemetry through Gigabyte SIV.
- [Lenovo](#lenovo) - board temperature telemetry through Lenovo Vantage Hardware Scan and fan telemetry through Lenovo GameZone WMI.

## Provider Model

- CaseDash uses Windows-native telemetry for generic CPU, memory, network, storage, drive activity, clock, and presented-FPS data.
- Hardware providers add hardware-specific GPU and board metrics when the matching driver, SDK, or utility is installed.
- The fake provider uses built-in synthetic telemetry when `/fake` has no path. Headless `/fake /exit` uses the static baseline, while UI fake mode and benchmark fake mode use the live synthetic source. UI fake mode advances at the same 250 ms cadence and retained-throughput smoothing as real telemetry.
- Unsupported or unavailable hardware providers do not prevent the dashboard from running; their provider-owned values render as unavailable.
- GPU telemetry selects the supported hardware provider from the configured GPU adapter identity. Empty GPU configuration selects the first non-software DXGI adapter.
- On hybrid laptops, the integrated adapter can be the first DXGI adapter even when a discrete GPU is also installed; selecting a different adapter in the dashboard devices menu recreates the matching vendor provider for that adapter.
- The displayed GPU product name uses the selected DXGI adapter description when available; provider diagnostics can still include lower-level runtime device names.
- Vendor GPU providers match their runtime device handle to the selected DXGI adapter by PCI identity when the driver API exposes enough detail, then fall back to provider name matching.
- Board telemetry selects the supported hardware provider from the baseboard manufacturer.
- Trace output can include `gpu_vendor:*` and `board_vendor:*` selection details, provider-specific diagnostics, and unsupported-provider fallback markers.
- Layout metric references are the source of truth for requested logical board metrics. The `[board]` mapping connects logical names to provider-specific sensor names.
- Empty CPU, GPU, and system board bindings use first-use auto-detection from the active provider's sensor names; the system binding also accepts motherboard or board sensor names. Otherwise, bound board metrics resolve when the mapped sensor exists. The GPU board fan binding is requested by `gpu.fan` as a fallback source and does not need to appear as a `board.fan.gpu` widget metric. The CPU board temperature binding is requested by `gpu.temp` as the Intel fallback source.
- The CaseDash service IPC exposes `presented_fps_sample` for FPS and `board_sensors_sample` for board providers that need LocalSystem access to hardware interfaces.

## Adding Hardware Support

- Provider selection stays split into three steps: extract adapter identity, map vendor information to the vendor enum, and create the matching provider with the full adapter identity.
- GPU extraction reads the selected non-software DXGI adapter identity into `GpuAdapterInfo`, including PCI vendor id, PCI device id, subsystem id, revision, PCI bus address when Windows reports it, adapter index, dedicated-memory size, and adapter name. `GpuAdapterInfo` inherits the `GpuVendorInfo` vendor id and adapter name used by vendor mapping, so vendor providers keep the full adapter identity for runtime device matching without broadening the selection contract.
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

## Intel

- Supported hardware family: Intel GPU telemetry.
- Runtime dependency: Intel display driver with the Level Zero Sysman loader (`ze_loader.dll`) available.
- Metrics include provider-supplied GPU telemetry such as engine load, temperature, clock, device-local memory, and fan speed when the driver exposes those Sysman components.
- Integrated GPUs commonly expose no device-local memory or fan component; in those cases CaseDash keeps VRAM on the generic Windows dedicated-memory fallback and renders fan speed unavailable. When Level Zero exposes no native temperature sensor for the selected Intel GPU, CaseDash falls back to the resolved board CPU temperature for `gpu.temp` because integrated CPU and GPU telemetry describe the same package temperature source.
- Presented FPS is the smoothed rolling presented-FPS rate from Windows DXGI, D3D9, or fallback DxgKrnl ETW present events because Level Zero Sysman has no native game-FPS metric.
- Trace output can include `intel_level_zero:*` provider details and `unsupported_gpu` fallback markers.

Troubleshooting:

1. Install or update the Intel graphics driver.
2. Confirm `ze_loader.dll` is present in `C:\Windows\System32`.
3. Run the matching dump or trace validation flow from [docs/diagnostics.md](diagnostics.md).
4. Inspect the exported dump and trace outputs for Level Zero Sysman component counts and provider state on that machine.

## NVIDIA

- Supported hardware family: NVIDIA GPU telemetry.
- Runtime dependency: NVIDIA display driver with NVML available.
- Metrics include NVML provider telemetry such as dedicated memory, temperature, and fan speed. GPU load uses the Windows GPU Engine counters because `nvmlDeviceGetUtilizationRates` can intermittently return `Unknown Error` on WDDM laptop drivers. GPU clock uses NVAPI when the dGPU is powered; when NVAPI reports `GPU not powered`, the clock value is unavailable for that sample instead of forcing the slower NVML clock path to wake or poll the idle dGPU.
- Presented FPS is the smoothed rolling presented-FPS rate from Windows DXGI, D3D9, or fallback DxgKrnl ETW present events because NVML has no native game-FPS metric.
- Trace output can include `nvidia_nvml:*` provider details, including NVAPI clock-selection markers, and `unsupported_gpu` fallback markers.

Troubleshooting:

1. Install or update the NVIDIA display driver.
2. Confirm `nvml.dll` is present in `C:\Windows\System32` or the NVIDIA NVSMI install directory.
3. Run the matching dump or trace validation flow from [docs/diagnostics.md](diagnostics.md).
4. Inspect the exported dump and trace outputs for the provider state on that machine.

## ASUS

- Supported hardware family: ASUS board telemetry.
- Runtime dependency: ASUS Armoury Crate or ASUS System Control Interface with the `\\.\ATKACPI` device available to the current user.
- Metrics include CPU temperature and laptop fan RPM telemetry from the same ATK `DSTS` device ids used by Armoury Crate support libraries. Device id `0x00120094` is exposed as `CPU Temperature`, `0x00110013` as `CPU Fan`, and `0x00110014` as the ASUS board-backed GPU fan fallback for `gpu.fan`.
- Trace output can include `asus_armoury_crate:*` provider details and `unsupported_board` selection markers.
- If Windows denies access to ATKACPI, CaseDash keeps the board metrics unavailable and reports the provider error in diagnostics.

Troubleshooting:

1. Install or update Armoury Crate or ASUS System Control Interface.
2. Confirm Armoury Crate can show the machine's fan and temperature telemetry from a normal user session.
3. Confirm the `\\.\ATKACPI` device opens for the current user; trace output reports `atk_driver_open_failed` when Windows blocks it.
4. Run the matching trace plus dump validation flow from [docs/diagnostics.md](diagnostics.md).
5. Inspect the dump for `board.*` values and the trace for `asus_armoury_crate:*` diagnostics.

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

## Lenovo

- Supported hardware family: Lenovo board telemetry on systems with the Lenovo Vantage Hardware Scan addin installed under `ProgramData\Lenovo\Vantage\Addins\LenovoHardwareScanAddin`.
- Runtime dependency: Lenovo Vantage Hardware Scan and its LdeApi diagnostics modules, including `LdeApi.Client.dll`, `LdeApi.Server.exe`, and the Lenovo diagnostics driver components installed by Lenovo platform software. Fan RPM uses Lenovo's `ROOT\WMI:LENOVO_GAMEZONE_DATA` methods.
- Metrics include requested Hardware Scan temperature telemetry from the storage, CPU, motherboard, video-card, and battery modules. Fan telemetry comes from `GetFanCount`, `GetFan1Speed`, and `GetFan2Speed` on Lenovo's GameZone WMI class. CaseDash names those readings as `Disk Temperature`, `CPU Temperature`, `Motherboard Temperature`, `GPU Temperature`, `Battery Temperature`, `Fan`, `CPU Fan`, and `GPU Fan`.
- The unelevated dashboard starts `CashDashService` `board_sensors_sample` queries asynchronously so the LocalSystem service can run the Lenovo diagnostics addin with the same privilege boundary Lenovo uses for hardware access without blocking dashboard startup. The provider reuses the last successful service sample while a refresh is running. When the service becomes absent or unreachable in a non-elevated dashboard, the provider clears cached service readings, marks requested Hardware Scan values with `!admin`, and still tries the GameZone WMI fan path as the last-resort fan source. Elevated runs without the service refresh the same Hardware Scan LdeApi path in the dashboard process in the background, reuse the last successful direct sample, and keep startup responsive while a slow direct scan is running.
- Lenovo Vantage's UI reaches the same diagnostics stack through the trusted `SystemManagement.HardwareScan.General` private RPC contract with the `DoExecutionThermalTool` command. CaseDash does not depend on that private trusted Vantage RPC endpoint or the older firmware interface for board telemetry.
- Trace output can include `lenovo_hardware_scan:*` provider details, `module_load_result` summaries for requested Lenovo temperature modules, `direct_snapshot_refresh_started` markers for background direct scans, `gamezone_wmi_*` markers for fan queries, and `unsupported_board` markers.

Troubleshooting:

1. Install or update Lenovo Vantage, Lenovo System Interface Foundation, and Lenovo platform drivers for the machine.
2. Confirm Lenovo Vantage Hardware Scan is installed and can show thermal telemetry from a normal user session.
3. Enable `Start with Windows` once from CaseDash when no-elevation Hardware Scan access is required; that installs and starts `CashDashService`.
4. Run the matching trace plus dump validation flow from [docs/diagnostics.md](diagnostics.md).
5. Inspect the dump for `board.*` values and the trace for `lenovo_hardware_scan:*` diagnostics.
