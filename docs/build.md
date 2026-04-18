# Build and development

This document is the single maintained source of truth for build prerequisites, build invocation, and developer-facing setup notes.

## Requirements

- Windows 11
- Visual Studio 2026 Insiders (`18`) Build Tools with CMake support
- Visual Studio 2026 Insiders (`18`) C++/CLI support
- .NET Framework 4.8 SDK
- vcpkg available either through the active Visual Studio developer environment or through a local install pointed to by `VCPKG_ROOT`, so the CMake configure step can resolve the repo manifest dependency on GoogleTest
- AMD Software: Adrenalin Edition with ADLX runtime available for AMD GPU telemetry
- Gigabyte SIV installed on supported Gigabyte motherboards when board temperature and fan telemetry are desired

## Current Toolchain

- `devenv.cmd` currently activates the Visual Studio 2026 Insiders (`18`) x64 developer environment.
- The active build compiler is MSVC `19.51.36231` for `x64`.
- The active LLVM tools are `clang-format` `20.1.8` and `clang-tidy` `20.1.8`.
- The active CMake executable in that environment is `4.2.3-msvc3`.

## Build

Always build with `build.cmd` from the repository root:

```bat
build.cmd
```

All build artifacts are kept under `build\`, except for the persistent repo-root `vcpkg\` manifest install tree that is intentionally kept outside `build\` so deleting `build\` for a clean configure does not force vcpkg to restore the GoogleTest dependency again.
`build.cmd` configures the CMake tree with `Ninja Multi-Config`, prefers the active developer environment's bundled vcpkg toolchain when `VSINSTALLDIR` provides it, otherwise falls back to `%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake`, directs that toolchain's manifest-mode configure step to install the repo `vcpkg.json` dependency set for the `x64-windows` triplet into `vcpkg\`, and restores `build\cmake\compile_commands.json` for `clangd`-based editors such as Zed.

## Install

Install the already-built runtime with `install.cmd` from the repository root:

```bat
install.cmd
```

`install.cmd` requests elevation, stops any running `SystemTelemetry.exe` instances across user sessions, waits for those processes to exit, installs `build\SystemTelemetry.exe` into `C:\Program Files\SystemTelemetry`, and does not run `build.cmd` on its own.

Run unit tests from the generated CMake tree after a successful build:

```bat
ctest --test-dir build\cmake -C Release --output-on-failure
```

Diagnostics validation commands and output expectations are documented in [docs/diagnostics.md](diagnostics.md).

## Telemetry provider notes

### AMD GPU metrics

AMD GPU metrics come from AMD's ADLX runtime.
On supported Radeon systems, this is typically installed with current AMD graphics drivers and provides GPU temperature, clock, and fan speed directly from AMD's API.

If AMD GPU metrics are missing:

1. Install or update AMD Software: Adrenalin Edition for your Radeon GPU.
2. Confirm `amdadlx64.dll` is present in `C:\Windows\System32`.
3. Run `build\SystemTelemetry.exe /dump`.
4. Check `telemetry_dump.txt` and `telemetry_trace.txt` in the command's working directory for the final snapshot and step-by-step provider diagnostics.

### Gigabyte board metrics

On supported Gigabyte systems, named board temperature and fan telemetry come from the app's in-process Gigabyte SIV integration, which loads the installed SIV .NET assemblies directly from native C++ code.

If those board metrics are missing:

1. Install Gigabyte SIV so its assemblies are present and registered.
2. Run `build\SystemTelemetry.exe /trace /dump /exit`.
3. Check `telemetry_dump.txt` for the rendered `board.*` metric values and `telemetry_trace.txt` for the `gigabyte_siv:*` trace lines and provider diagnostics.
