# Build And Development

This document owns build prerequisites, developer setup, build and test commands, install flow, and developer entrypoint scripts.
See also: [docs/project.md](project.md) for repository policy, [docs/diagnostics.md](diagnostics.md) for diagnostics validation commands, and [docs/architecture.md](architecture.md) for subsystem and build-graph structure.

## Requirements

- Windows 11
- Visual Studio 2026 Insiders (`18`) Build Tools with CMake support
- Visual Studio 2026 Insiders (`18`) C++/CLI support
- .NET Framework 4.8 SDK
- vcpkg available either through the active Visual Studio developer environment or through `VCPKG_ROOT`
- Graphviz `dot` available on `PATH` when rendering the optional source dependency SVG
- AMD Software: Adrenalin Edition when AMD GPU telemetry is required
- Gigabyte SIV installed when Gigabyte board temperature or fan telemetry is required

## Current Toolchain

- `devenv.cmd` activates the Visual Studio 2026 Insiders (`18`) x64 developer environment.
- The active native compiler is MSVC `19.51.36231` for `x64`.
- The active LLVM tools are `clang-format` `20.1.8` and `clang-tidy` `20.1.8`.
- The active CMake executable in that environment is `4.2.3-msvc3`.

## Build

Always build through the repository entrypoint:

```bat
build.cmd
```

`build.cmd` configures and builds the maintained CMake tree under `build\cmake\`, keeps the final executable under `build\`, preserves the repo-root `vcpkg\` manifest install tree across clean builds, and restores `build\cmake\compile_commands.json` for `clangd`-based editors.

## Test

Run unit tests after a successful build:

```bat
ctest --test-dir build\cmake -C Release --output-on-failure
```

Diagnostics validation commands live in [docs/diagnostics.md](diagnostics.md).

## Install

Install the already-built runtime through the repository entrypoint:

```bat
install.cmd
```

`install.cmd` requests elevation, stops running `SystemTelemetry.exe` instances, waits for them to exit, installs `build\SystemTelemetry.exe` into `C:\Program Files\SystemTelemetry`, and leaves auto-start registration to the runtime menu toggle.

## Developer Tooling Entrypoints

- `format.cmd` is the maintained entrypoint for formatting non-vendored C++ sources.
- `lint.cmd` is the maintained entrypoint for architecture checks, source dependency graph checks, include-path checks, header-body checks, and optional `clang-tidy` runs. Each lint run rebuilds the source dependency DOT and GraphML under `build\architecture\` without rendering SVG. The optional tidy sweep writes `build\clang_tidy_report.txt`, uses a one-minute per-file timeout, filters maintained include-cleaner false positives, and excludes `src\diagnostics\snapshot_dump.cpp` because the active Clang diagnostics stall on that translation unit.
- `profile_benchmark.cmd` is the maintained entrypoint for elevated benchmark profiling and daemon-backed benchmark requests.
- `devenv.cmd` is the maintained environment bootstrap for local builds and tool runs.

## Provider Notes

### AMD GPU telemetry

AMD GPU metrics come from the ADLX runtime installed with current Radeon drivers.

If AMD GPU metrics are missing:

1. Install or update AMD Software: Adrenalin Edition.
2. Confirm `amdadlx64.dll` is present in `C:\Windows\System32`.
3. Run the matching dump or trace validation flow from [docs/diagnostics.md](diagnostics.md).
4. Inspect the exported dump and trace outputs for the provider state on that machine.

### Gigabyte board telemetry

Gigabyte board metrics come from the in-process SIV integration that loads the installed vendor assemblies.

If board metrics are missing:

1. Install Gigabyte SIV.
2. Run the matching trace plus dump validation flow from [docs/diagnostics.md](diagnostics.md).
3. Inspect the dump for `board.*` values and the trace for `gigabyte_siv:*` diagnostics.
