## v0.3

Big usability improvement that makes CaseDash practical on a single monitor and easier to use for first-time users in general.

### UX

- Added hover titlebar controls for app menu, display setup, layout-edit mode, and close, with shared tooltips and native hover or pressed states.
- Added native auto-hide drawer behavior for edge placements, including snapshot-backed slide animation and topmost show handling.
- Improved Configure Display with edge placements, active placement schematics, saved selection markers, monitor scale labels, and corrected fullscreen placement offsets.
- Added the built-in `1x4` portrait layout and titleless card placements for compact vertical dashboards.
- Added paired MB/GB formatting for VRAM metrics so small dedicated-memory values display in MB and larger capacities display in GB.

### Telemetry

- Added Lenovo board telemetry for supported Lenovo Vantage systems, with diagnostics-driver CPU temperature sampling and Lenovo GameZone WMI fan telemetry.
- Added stable GPU adapter selection names and selected-adapter FPS filtering so duplicate GPU names, idle adapters, and powered-off adapters report the intended values.
- Improved GPU telemetry with NVIDIA NVAPI clock sampling, Intel Level Zero fallback support for older loaders, transient NVML failure caching, and powered-off GPU clock/FPS values shown as zero.

### Misc 

- Improved auto-start setup with service startup checks, rollback when Run registration fails, and cleaner service removal handling.
- Fixed advanced-menu Alt and Alt+right-click cancellation behavior.

---
## v0.2

- Added live dashboard animation for metric bars, gauges, drive activity, and throughput plots.
- DXGI swap-chain vsync-paced animation spends less than 1 ms per frame.
- Telemetry metrics update every 250 ms, and values interpolate smoothly between telemetry updates during animation.
- Added Intel GPU telemetry through Level Zero Sysman.
- Added ASUS Armoury Crate board telemetry.
- Supported machines with multiple GPUs via configuration/UI switch of the current GPU.

---
## v0.1.1

- Improved layout-edit drag feedback so guides, handles, and dragged content track continuous pointer movement more immediately.
- Reduced the shipped executable below 1 MB through compressed resources, leaner embedded atlases, smaller runtime metadata, and tighter cold-path code shape.
- Added branded .msi installer dialog and banner artwork generated during packaging.
- Fixed installer completion launch behavior that could trigger error 2753.
- Fixed throughput graph scaling so smoothed graph maxima stay stable when retained history scrolls.

---
## v0.1

- Initial release.
