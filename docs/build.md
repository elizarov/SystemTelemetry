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
- NVIDIA display driver with NVML when NVIDIA GPU telemetry is required
- Gigabyte SIV installed when Gigabyte board temperature or fan telemetry is required
- GitHub Actions uses the `windows-2025-vs2026` runner for push and pull request build, test, format, lint, and tidy validation.

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

`build.cmd` configures and builds the maintained CMake tree under `build\cmake\`, keeps the final executable under `build\`, preserves the repo-root `vcpkg\` manifest install tree across clean builds, restores `build\cmake\compile_commands.json` for `clangd`-based editors, and keeps vcpkg download plus registry caches in a user-local cache root so fresh worktrees reuse the same bootstrap downloads.

By default the shared cache root is `%LOCALAPPDATA%\SystemTelemetry\cache`, falls back to `%USERPROFILE%\.systemtelemetry\cache` when `LOCALAPPDATA` is unavailable, and can be overridden with `SYSTEMTELEMETRY_CACHE_ROOT`. `build.cmd` exports `VCPKG_DOWNLOADS` and `X_VCPKG_REGISTRIES_CACHE` from that root unless the caller already set them.

## Test

Run unit tests after a successful build:

```bat
test.cmd
```

Diagnostics validation commands live in [docs/diagnostics.md](diagnostics.md).

## Install

Install the already-built runtime through the repository entrypoint:

```bat
install.cmd
```

`install.cmd` requests elevation, stops running `SystemTelemetry.exe` instances, waits for them to exit, installs `build\SystemTelemetry.exe` into `C:\Program Files\SystemTelemetry`, and leaves auto-start registration to the runtime menu toggle. The auto-start toggle installs the machine-wide Run entry for per-user dashboard startup and the `SystemTelemetryFpsService` LocalSystem service used for privileged FPS collection.

## Developer Tooling Entrypoints

- `format.cmd` is the maintained entrypoint for formatting non-vendored C++ sources. Its changed-file mode keeps Git CRLF normalization warnings out of formatter file discovery.
- `test.cmd` is the maintained entrypoint for running the CTest suite against the built Release tree, and it prints verbose CTest output so GitHub runner logs show each test command and its stdout even when tests pass.
- The repo `pre-commit` hook launches `tools\pre_commit_checks.ps1`, which formats staged eligible C++ files through `tools\run_clang_format.ps1` and runs `lint.cmd` before each commit. Git file discovery starts from all staged `.cpp` and `.h` paths, then the shared formatter filter limits work to maintained non-vendored `src\` and `tests\` files. The hook temporarily stashes unstaged and untracked files while it formats and lints the staged snapshot, restores them before exit, and aborts the commit when formatting or lint checks fail.
- `lint.cmd` is the maintained entrypoint for architecture checks, source dependency graph checks, include-path checks, header-body checks, and optional `clang-tidy` runs. Each lint run rebuilds the source dependency DOT and GraphML under `build\architecture\` without rendering SVG. The optional tidy sweep checks maintained non-vendored `.cpp` and `.h` files under `src\` and `tests\`, excludes `board_gigabyte_siv.cpp`, `board_gigabyte_siv_bridge.cpp`, and `board_msi_center_bridge.cpp`, writes `build\clang_tidy_report.txt`, uses a four-minute per-file timeout, reports enabled analyzer, bugprone, unused internal function, and unused include findings as errors, and filters maintained include-cleaner false positives.
- `lint.cmd tidy` runs a full optional `clang-tidy` sweep and commonly needs at least eight minutes on the current toolchain before it can report success or failure. Local development avoids this slow sweep unless explicitly requested. GitHub Actions owns the routine tidy sweep with `SYSTEMTELEMETRY_TIDY_TIMEOUT_SECONDS` set to a larger per-file timeout and `SYSTEMTELEMETRY_TIDY_MAX_PARALLEL` set for runner stability.
- CMake enables MSVC warning C4505 and treats it as an error so unreferenced internal functions are caught during normal builds when MSVC can diagnose them.
- Native C++ targets compile with `/GR-`; production code uses explicit project type tags instead of native RTTI. The C++/CLI Gigabyte bridge keeps managed casts in its `/clr` translation unit.
- Release app and benchmark builds compile size-oriented code with `/Os` and `/GL`, then link with `/LTCG`, `/OPT:REF`, and non-incremental linking so whole-program optimization and reference elimination reduce the shipped executable while benchmarks measure the same optimization profile. Benchmark-sensitive renderer, widget, layout, telemetry, and benchmark-harness translation units retain `/O2` inside that Release profile so size work does not distort the maintained performance loops. Tests keep the normal Release compile/link path for faster local validation.
- `profile_benchmark.cmd` is the maintained entrypoint for elevated benchmark profiling and daemon-backed benchmark requests. Daemon-backed requests write the ETL, xperf detail summary, process-filtered call tree, hotspot summary, and benchmark stdout under `build\profile_benchmark_daemon\requests\`.
- `devenv.cmd` is the maintained environment bootstrap for local builds and tool runs. GitHub Actions does not use this machine-local script; `build.cmd`, `format.cmd`, and `lint.cmd tidy` resolve Visual Studio and LLVM tools from the runner environment.

## GitHub Validation

- The `Validation` workflow runs on every push, pull request, and manual dispatch.
- The workflow restores the shared vcpkg download and registry caches under `.github-cache\SystemTelemetry` inside the checked-out workspace before validation, then saves the refreshed cache contents after the run so repeated GitHub-hosted runs reuse the same bootstrap downloads.
- The workflow checks formatting first with `format.cmd`, then builds with `build.cmd`, runs tests with `test.cmd`, and runs `lint.cmd tidy` on `windows-2025-vs2026`.
- The repository branch protection requires the `Validation` job before pull requests can merge.
- The workflow uploads `build\SystemTelemetry.exe` as the `SystemTelemetry-exe` artifact after validation succeeds.
- The workflow uploads `build\clang_tidy_report.txt` as an artifact when it is produced.

## Provider Notes

### AMD GPU telemetry

AMD GPU metrics come from the ADLX runtime installed with current Radeon drivers.

If AMD GPU metrics are missing:

1. Install or update AMD Software: Adrenalin Edition.
2. Confirm `amdadlx64.dll` is present in `C:\Windows\System32`.
3. Run the matching dump or trace validation flow from [docs/diagnostics.md](diagnostics.md).
4. Inspect the exported dump and trace outputs for the provider state on that machine.

### NVIDIA GPU telemetry

NVIDIA GPU metrics come from the NVML runtime installed with current NVIDIA display drivers.
The NVIDIA FPS value comes from Windows DXGI/D3D9 ETW present events. When auto-start is enabled, the same executable also runs as the `SystemTelemetryFpsService` LocalSystem service and serves only the FPS sample to unelevated dashboards through a named pipe. Without that service, the dashboard falls back to local ETW collection, so the process needs permission to start a real-time ETW session, such as elevation or membership in the local `Performance Log Users` group. When Windows denies that access, the dashboard reports `Need admin` for FPS instead of treating it as ordinary unavailable data.

If NVIDIA GPU metrics are missing:

1. Install or update the NVIDIA display driver.
2. Confirm `nvml.dll` is present in `C:\Windows\System32` or the NVIDIA NVSMI install directory.
3. Run the matching dump or trace validation flow from [docs/diagnostics.md](diagnostics.md).
4. Inspect the exported dump and trace outputs for the provider state on that machine.

### Gigabyte board telemetry

Gigabyte board metrics come from the in-process SIV integration that loads the installed vendor assemblies.

If board metrics are missing:

1. Install Gigabyte SIV.
2. Run the matching trace plus dump validation flow from [docs/diagnostics.md](diagnostics.md).
3. Inspect the dump for `board.*` values and the trace for `gigabyte_siv:*` diagnostics.
