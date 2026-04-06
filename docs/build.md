# Build and development

This document is the single maintained source of truth for build prerequisites, build invocation, and developer-facing setup notes.

## Requirements

- Windows 11
- Visual Studio 2022 Build Tools with CMake support
- Visual Studio 2022 C++/CLI support
- .NET Framework 4.8 SDK
- AMD Software: Adrenalin Edition with ADLX runtime available for AMD GPU telemetry
- Gigabyte SIV installed on supported Gigabyte motherboards when board temperature and fan telemetry are desired

## Build

Always build with `build.cmd` from the repository root:

```bat
build.cmd
```

All build artifacts are kept under `build\`.

Diagnostics validation commands and output expectations are documented in [docs/diagnostics.md](/D:/Projects/SystemTelemetry/docs/diagnostics.md).

## Telemetry provider notes

AMD GPU metrics come from AMD's ADLX runtime instead of LibreHardwareMonitor or OpenHardwareMonitor. On supported Radeon systems, this is typically installed with current AMD graphics drivers and provides GPU temperature, clock, and fan speed directly from AMD's API.

If AMD GPU metrics are missing:

1. Install or update AMD Software: Adrenalin Edition for your Radeon GPU.
2. Confirm `amdadlx64.dll` is present in `C:\Windows\System32`.
3. Run `build\SystemTelemetry.exe /dump`.
4. Check `build\telemetry_dump.txt` for the final snapshot and `build\telemetry_trace.txt` for the step-by-step provider diagnostics.

On supported Gigabyte systems, named board temperature and fan telemetry come from the app's in-process Gigabyte SIV integration, which loads the installed SIV .NET assemblies directly from native C++ code.

If those board metrics are missing:

1. Install Gigabyte SIV so its assemblies are present and registered.
2. Run `build\SystemTelemetry.exe /trace /dump /exit`.
3. Check `build\telemetry_dump.txt` for the `board_provider.*` block and `build\telemetry_trace.txt` for the `gigabyte_siv:*` trace lines.
