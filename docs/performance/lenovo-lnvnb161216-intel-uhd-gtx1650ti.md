# Lenovo LNVNB161216 + Intel UHD + NVIDIA GTX 1650 Ti Performance

This file records current benchmark ranges, bottlenecks, and next research directions for this test machine. It intentionally does not keep a journal of past experiments.

## Machine Details

- Board: LENOVO LNVNB161216, version `SDK0J40697 WIN`, system model `20V3`.
- CPU: Intel Core i7-10750H, `6` cores and `12` logical processors.
- GPUs: Intel UHD Graphics, DXGI dedicated memory about `0.12 GiB`; NVIDIA GeForce GTX 1650 Ti with Max-Q Design, DXGI dedicated memory about `3.84 GiB`.
- System memory: `15.87 GiB` visible, installed as `2 x 8 GB` Samsung `M471A1K43DB1-CWE` at `2933 MT/s`.
- Hardware providers used by current traces: Lenovo Hardware Scan and GameZone WMI for board telemetry, Intel Level Zero for the default selected GPU, NVIDIA NVML/NVAPI when the NVIDIA adapter is selected, and Presented FPS ETW when `gpu.fps` is requested and elevated or service access is available.

## Current Benchmark Ranges

Ranges use direct `build\CaseDashBenchmarks.exe` runs unless a note says otherwise. Elevated telemetry ranges use `profile_benchmark.cmd /direct ...` through the elevated benchmark daemon because this machine requires elevation for vendor telemetry.

| Benchmark | Current known range | Important split |
| --- | --- | --- |
| `animation 240 2` | `animation_loop` and `animation_frame` about `0.65-1.53 ms` | Current direct reruns report `snapshot_animations=28`, `overlay_animations=0`, and `active_chunk_frames=120`. |
| `snapshot-handoff 20 2` | `snapshot_loop` about `8.3-10.6 ms` | `presentation_frame_build` carries nearly all cost; `presentation_frame_publish` stays near `0.00-0.01 ms`. |
| `edit-layout 240 2` | `drag_loop` about `3.63-4.17 ms` | `snap` about `0.12-0.14 ms`, `apply` about `0.09-0.11 ms`, `paint_draw` about `3.38-3.89 ms`. |
| `layout-guide-sheet 20 2` | `sheet_loop` about `192-223 ms` | `sheet_place` is dominant at about `100-118 ms`; `sheet_draw` is about `58-63 ms`. |
| `layout-switch 240 2` | `switch_loop` about `8.89-11.78 ms` | `switch_apply` about `1.48-2.04 ms`, `dialog_refresh` about `0.33-0.43 ms`, `switch_paint` about `7.03-9.26 ms`. |
| `mouse-hover 240 2` | `hover_loop` about `1.50-1.53 ms` | `hover_hit_test` about `0.38-0.44 ms`; repaint is about `1.07-1.13 ms`. |
| `theme-change 240 2` | `theme_loop` about `5.38-6.73 ms` | `dashboard_config` about `1.25-1.57 ms`, `theme_preview` about `0.44-0.48 ms`, `theme_paint` about `3.26-4.16 ms`. |
| `telemetry-init 1 2`, default layout with synchronous provider samples | `iteration_loop` about `5.6-15.0 s` | `collector_initialize` carries nearly all cost; `collector_destroy` stays about `1-4 ms` after synchronous provider sampling keeps Lenovo work out of teardown. |
| `telemetry-init 1 2`, no board-backed metrics | `iteration_loop` about `0.38-0.62 s` direct, about `6.7-12.0 s` under xperf | Direct runs show the baseline collector setup is sub-second when no Lenovo temperature is requested. Xperf materially distorts this path and attributes most samples to CPU PDH counter setup plus CLR/kernel work. |
| `telemetry-init 1 2`, fan-only Lenovo board metrics | `iteration_loop` about `0.37-0.74 s` direct | Non-elevated GameZone WMI fan access fails or returns quickly on this machine; no Hardware Scan temperature module is loaded. |
| `telemetry-init 1 2`, Lenovo temperature-only board metrics | `iteration_loop` about `8.0-14.8 s` direct | A single requested CPU temperature is enough to trigger the slow Lenovo Hardware Scan temperature path. |
| `lenovo-gamezone 5 2`, elevated direct daemon | `gamezone_sample` about `26-34 ms` | `GetCPUTemp` and `GetGPUTemp` return raw `0`, so GameZone WMI is cheap but not a usable temperature source on this machine. Fan methods return `CPU Fan` and `GPU Fan` around `3100-3500 RPM`. |
| `lenovo-lde-phases 1 2`, elevated direct daemon | Manual CPU thermal-tool execution cold path about `11.1-19.7 s` and no telemetry; `GetAvailableModules` plus manual execution about `34.6-41.5 s` and no telemetry; normal CPU `LoadModules` plus execution about `35.3-43.8 s` with valid telemetry | The manual LDE payload uses module `13`, device `0`, and tool `90`, but still pays server/native initialization inside `StartExecution` and does not bypass module loading. `GetAvailableModules` is not cheap; it performs the same cold LDE warmup class of work before returning `25` modules. |
| `lenovo-hardware-scan 3 2`, elevated direct daemon | Cold CPU-only scan about `6.1-11.7 s` direct after removing the diagnostic status request; warmed runtime samples about `31-50 ms` | The same Lenovo Hardware Scan thermal data path returns CPU package and per-core temperatures quickly once the LdeApi server and CPU module are loaded. The slow startup work is the first LdeApi server/module operation, not the temperature read itself. |
| `temperature-sources 3 2`, elevated direct daemon | Valid temperature source: NVIDIA NVML, `53-54 C`, about `31-36 ms` including provider init/sample in the repeated probe | Lenovo GameZone and broader Lenovo WMI getters are cheap but return no valid temperatures; `GetCPUTemp` and `GetGPUTemp` return `0`. Windows ACPI thermal-zone, performance thermal-zone, and storage WMI return no rows. LibreHardwareMonitor and OpenHardwareMonitor WMI namespaces are absent. Intel Level Zero reports no temperature sensors for the Intel UHD adapter. |
| `update-telemetry 20 2`, non-elevated default with synchronous provider samples | `update_loop` about `41.9 ms` | `telemetry_update` about `38.3 ms`, `paint_draw` about `3.6 ms`; initialization is not included in the loop line. |
| `update-telemetry 20 2`, profiled elevated default Intel GPU with synchronous provider samples | `update_loop` about `61.0 ms` under xperf | `telemetry_update` about `57.4 ms`, `paint_draw` about `3.6 ms`; the profile still shows initialization samples, so treat this as hotspot attribution rather than a repeatable direct baseline. |
| Elevated no-board and NVIDIA `update-telemetry` variants | Needs refresh | Previous ranges predate synchronous provider samples in the benchmark and are no longer comparable. |

## Current Bottlenecks

- Direct telemetry initialization is Lenovo temperature-provider bound on the default layout. Removing board-backed temperature and fan metrics drops direct startup setup below one second; requesting only fan metrics stays below one second; requesting one CPU temperature brings the multi-second Lenovo Hardware Scan path back.
- Xperf and the elevated profiling daemon can make even no-board telemetry initialization look multi-second. Treat those traces as attribution only: on this machine the profiled no-board path is dominated by `AddCounterCompat`, CPU PDH setup, CLR/JIT, kernel, and filter-driver work that does not match direct wall-clock baselines.
- The expensive Lenovo data shape is the Hardware Scan temperature path. Elevated GameZone WMI sensor access is cheap and exposes fan RPM on this machine, but its CPU and GPU temperature methods return raw `0`, so it cannot replace Hardware Scan for the default temperature metrics here.
- Lenovo Hardware Scan temperature reads are cheap after warmup. The addin loads `thermal_monitor_tool.dll` and `lde_module_cpu.dll` through the LdeApi diagnostics runtime, then emits `temperatureCelsius` and `temperatureCoresCelsius` execution messages. The cold path spends seconds in the first LdeApi operation. After removing the unused diagnostic `GetStatus` request, `LoadModules` itself still blocks for about `5.9-11.5 s` before returning its already-completed task; warmed CPU-only execution returns in tens of milliseconds.
- Lenovo's LdeApi client methods are synchronous wrappers that return `Task.FromResult` after the RPC request finishes. The first request starts `LdeApi.Server.exe`, validates `LdeApi.Server.exe`, `lsdk.dll`, and launcher binaries through Lenovo certificate validation, waits for the server-ready mutex, and then performs module loading. The profiled cold scan is dominated by CLR/JIT, kernel work, `Lenovo.CertificateValidation.Native.dll`, WinTrust, and crypto modules, so there is no cheap await or shorter task wait to tune inside the public LdeApi call.
- LDE's next layer down is private native SDK state. `LdeApi.dll` builds an `LsdkObjects` graph around `lsdk.dll`, while `LdeApi.Marshal.Lde.dll` exposes managed bindings for module, device, test, tool, and execution calls. `lsdk.dll` also exports a private `LSDK_Performance_*` ABI, including `initialize`, `reloadModules`, and `getPerformanceData`; a direct scratch probe shows those calls are cheap but fail with no data unless the signed Lenovo LDE initialization succeeds.
- The Hardware Scan manager's private thermal route maps to LdeApi tool execution, not a separate public temperature getter. Lenovo Vantage calls the trusted Hardware Scan RPC command `DoExecutionThermalTool`; the addin maps that request to module `13`, device `0`, tool `90`, and LdeApi `StartExecution`. A CaseDash probe with the same IDs proves that manual payload construction does not produce telemetry before the module service has loaded modules.
- `thermal_monitor_tool.dll` contains the actual thermal payload names and imports `DeviceIoControl`, `SETUPAPI`, `ADVAPI32`, `Qt6Core.dll`, and Lenovo diagnostics symbols. The likely faster source is below LdeApi in Lenovo's native driver/tool layer, but it is a private C++/Qt ABI rather than a stable C or COM/WMI API. Keep that path experimental unless a measured model-specific integration can avoid the signature-gated SDK warmup safely.
- The only cheap valid temperature found by the elevated local-source probe is NVIDIA NVML for the discrete GPU. That can serve a selected NVIDIA adapter's `gpu.temp`, but it is not a CPU/package or selected-Intel temperature replacement for the default layout.
- Presented FPS is not the dominant default elevated bottleneck on this machine. The provider is now demand-driven by the runtime config, so layouts that omit `gpu.fps` do not start the service/local ETW Presented FPS path.
- Intel Level Zero is cheap compared with Lenovo board collection once board rows are removed, but its exact current range needs refresh under synchronous provider samples.
- Telemetry initialization is provider-shaped. With benchmark synchronous provider samples enabled, `collector_initialize` takes about `5.6-15.0 s` on the default layout; direct overlay comparisons identify Lenovo Hardware Scan CPU-temperature capture as the wall-clock trigger, while xperf attributes sampled CPU to CLR/JIT, Lenovo certificate validation through WinTrust and crypto modules, CPU PDH counter setup, and Intel Level Zero/GPU adapter selection under `RealTelemetryCollector::Initialize`.
- Render-side benchmarks are slower than the Gigabyte desktop baseline. Direct2D, DirectWrite, text shaping, and the Intel/NVIDIA laptop display-driver stack dominate repaint-heavy paths after app-side hit testing, snap, apply, and config work stay small.
- Daemon-backed config overlays must use absolute paths. Relative overlay paths can resolve from the elevated daemon working directory and silently fall back to the embedded config.

## Lenovo Hardware Scan Startup Breakdown

Wall-clock phase timings come from the benchmark's internal timers. CPU timings are xperf profile weights from profiled elevated `lenovo-hardware-scan 1 2` runs with a 1 ms profile interval, grouped across `CaseDashBenchmarks.exe`, `LdeApi.Server.exe`, and Lenovo's `qt_server_app.exe` helper. Treat CPU timings as sampled CPU milliseconds, not elapsed wall time.

| Cold scan run | Wall-clock total | Bridge initialization | CPU `LoadModules` request | Thermal execution | Warmed execution |
| --- | --- | --- | --- | --- | --- |
| Faster profiled cold run | `6551.54 ms` | `86.50 ms` | `6293.91 ms` | `150.47 ms` | Not measured in that run |
| Slower profiled cold run | `15373.96 ms` | `50.15 ms` | `15148.72 ms` | `161.67 ms` | Not measured in that run |
| Fresh retained-runtime direct run | `11467.87 ms` | `42.38 ms` | `11263.17 ms` | `149.13 ms` | `38.41 ms` average for the next two samples |

The profiled runs show that wall-clock variability is inside the first synchronous LDE module request. The sampled CPU shape stays much more stable than the elapsed time:

| CPU component group | Faster profiled run | Slower profiled run | Interpretation |
| --- | --- | --- | --- |
| Kernel, filesystem, and filter drivers | About `1299 ms` | About `1346 ms` | Native process startup, DLL/file loading, filesystem filters, and kernel work dominate sampled CPU. |
| CLR, JIT, and managed runtime | About `846 ms` | About `848 ms` | Both the client process and `LdeApi.Server.exe` JIT managed LDE code on the cold path. |
| Lenovo certificate validation, WinTrust, and crypto | About `478 ms` | About `592 ms` | `bcryptprimitives.dll`, `CRYPT32.dll`, `WINTRUST.dll`, `cryptnet.dll`, and `Lenovo.CertificateValidation.Native.dll` are visible in the server and client process samples. |
| `ntdll.dll` and `KernelBase.dll` user-mode syscall/wait glue | About `409 ms` | About `395 ms` | Most of the synchronous request is wrapped in Windows call and wait infrastructure. |
| Lenovo native SDK and thermal tool modules | About `47 ms` | About `59 ms` | Direct sampled CPU in `lsdk.dll`, `thermal_monitor_tool.dll`, `Qt6Core.dll`, and nearby native support is small compared with startup and validation. |
| RPC, COM, loader, and miscellaneous user-mode libraries | About `123 ms` | About `163 ms` | Residual process startup, RPC, COM, loader, and support-library work. |

The split by process is also stable: the faster profiled run consumed about `936 ms` sampled CPU in `CaseDashBenchmarks.exe`, `2124 ms` in `LdeApi.Server.exe`, and `154 ms` in `qt_server_app.exe`; the slower profiled run consumed about `740 ms`, `2517 ms`, and `163 ms` respectively. This means the extra wall-clock delay is not proportional extra CPU work inside the thermal read. It is blocking startup/module orchestration inside Lenovo's synchronous LDE request.

## Further Research Directions

- For Lenovo telemetry, compare default elevated direct runs and `telemetry-init` against the same branch with `CashDashService` installed and running. If service-backed board samples collapse dashboard-process initialization or refresh cost, keep Lenovo Hardware Scan out of the dashboard process whenever possible.
- If direct elevated Lenovo board collection must remain, keep the Lenovo Hardware Scan runtime resident and warmed in a helper process or service instead of recreating it for each measurement. The current in-process cold diagnostics stack is too expensive for benchmark bursts and first display, while warmed thermal execution is fast enough for regular refreshes. Startup UI must not wait for a cold LdeApi `LoadModules` call; it should show unavailable board temperature or the current fan-only GameZone data until the background Hardware Scan snapshot completes.
- Keep the `lenovo-gamezone` benchmark available when testing other Lenovo models. If `GetCPUTemp` or `GetGPUTemp` return nonzero sane values elsewhere, prefer them for that model's default CPU/GPU temperature shape and keep Hardware Scan for storage, motherboard, battery, or unknown temperature requests.
- Keep `temperature-sources` available for new Lenovo and hybrid-laptop investigations. On this machine, do not add Windows ACPI thermal-zone, storage WMI, LibreHardwareMonitor/OpenHardwareMonitor WMI, or Intel Level Zero fallback code for CPU/package temperature unless a future driver update makes those paths report real values.
- Add finer provider-phase timing for `UpdateBoardMetrics`, `UpdateGpuMetrics`, and PDH collectors when the flat xperf summary is too coarse; the current call-tree export shows kernel, PDH, COM, CLR, Direct2D, and DirectWrite time but under-symbolizes the vendor leaves.
- Use absolute, ASCII/no-BOM overlay files for elevated daemon benchmark variants until config parsing grows BOM-tolerant section handling.
- Recheck layout-guide-sheet placement after any routing changes; this laptop is a good stress case for high-placement and high-draw costs.
