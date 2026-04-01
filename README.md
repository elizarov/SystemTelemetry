System Telemetry is a compact Windows 11 dashboard for an 800x480 secondary display that shows the telemetry this app can collect cleanly from Windows and vendor APIs: CPU load, clock, and RAM usage; GPU load, temperature, clock, fan speed, and VRAM usage; plus network throughput, storage usage, and time, all rendered in a high-contrast Win32 interface built in C++20.

## Requirements

- Windows 11
- Visual Studio 2022 Build Tools
- AMD Software: Adrenalin Edition with ADLX runtime available for AMD GPU telemetry

## AMD GPU Telemetry

AMD GPU metrics come from AMD's ADLX runtime instead of LibreHardwareMonitor or OpenHardwareMonitor. On supported Radeon systems, this is typically installed with current AMD graphics drivers and provides GPU temperature, clock, and fan speed directly from AMD's API.

If AMD GPU metrics are missing:

1. Install or update AMD Software: Adrenalin Edition for your Radeon GPU.
2. Confirm `amdadlx64.dll` is present in `C:\Windows\System32`.
3. Run `build\SystemTelemetry.exe /dump`.
4. Check `build\telemetry_dump.txt`, which now contains both step-by-step trace lines and the final telemetry snapshot written through one shared dump file stream.

## Build

Always build with `build.cmd` from the repository root:

```bat
build.cmd
```

All build artifacts are kept under `build\`.
