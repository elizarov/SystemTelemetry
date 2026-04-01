System Telemetry is a compact Windows 11 dashboard for an 800x480 secondary display that shows CPU and GPU activity, memory, network throughput, storage usage, and time in a high-contrast Win32 interface designed for quick glanceability. It is built in C++20 with Visual Studio 2022 and includes a `/dump` mode for writing the current telemetry snapshot and raw sensor discovery details to `telemetry_dump.txt` for debugging.

## Requirements

- Windows 11
- Visual Studio 2022 Build Tools
- LibreHardwareMonitor running with WMI enabled so the app can read CPU/GPU temperatures, power, clocks, and fan sensors

## LibreHardwareMonitor Installation

1. Download the latest LibreHardwareMonitor release from the official releases page: [LibreHardwareMonitor Releases](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases)
2. Extract the release zip to a stable folder on your machine.
3. Start `LibreHardwareMonitor.exe`.
4. In LibreHardwareMonitor, enable WMI support and keep the app running while System Telemetry is running.
5. If sensor access is still missing, restart LibreHardwareMonitor as Administrator and run `build\SystemTelemetry.exe /dump` to verify that `telemetry_dump.txt` shows `root\LibreHardwareMonitor` sensors instead of missing namespace errors.

## Build

Always build with `build.cmd` from the repository root:

```bat
build.cmd
```

All build artifacts are kept under `build\`.
