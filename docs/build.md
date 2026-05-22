# Build And Development

This document owns build prerequisites, developer setup, build and test commands, install flow, and developer entrypoint scripts.
See also: [docs/project.md](project.md) for repository policy, [docs/hardware.md](hardware.md) for supported hardware-provider requirements, [docs/diagnostics.md](diagnostics.md) for diagnostics validation commands, [docs/release.md](release.md) for release publication, [docs/optimize_size.md](optimize_size.md) for executable-size research, and [docs/architecture.md](architecture.md) for subsystem and build-graph structure.

## Requirements

- Windows 11
- Visual Studio 2026 Insiders (`18`) Build Tools with CMake support
- Visual Studio 2026 Insiders (`18`) C++/CLI support
- .NET Framework 4.8 SDK
- Python 3 available on `PATH` or discoverable by CMake for generated build resources and repository tooling
- Node.js and npm available on the host for regenerating the tree-sitter C++ overlay grammar used by native repository tools
- vcpkg available either through the active Visual Studio developer environment or through `VCPKG_ROOT`
- NuGet package restore access for the WiX Toolset SDK when building the MSI package; the installer project accepts the WiX 7 OSMF EULA for non-interactive local and CI builds.
- Provider software or drivers for optional hardware-provider telemetry as described in [docs/hardware.md](hardware.md)
- GitHub Actions uses the `windows-2025-vs2026` runner for pull request, main-branch push, and manual build, test, format, lint, and unused-include validation.

## Current Toolchain

- `devenv.cmd` activates the Visual Studio 2026 Insiders (`18`) x64 developer environment.
- The active native compiler is MSVC `19.51.36231` for `x64`.
- The active LLVM tools are `clang-format` `20.1.8` and `clangd` `20.1.8`.
- The active CMake executable in that environment is `4.2.3-msvc3`.

## Build

Always build through the repository entrypoint:

```bat
build.cmd
```

`build.cmd` configures the maintained CMake tree under `build\cmake\` only when the cache is missing, the generator changes, `CASEDASH_LINK_MAPS` changes, or `CASEDASH_FORCE_CONFIGURE` is set. Normal repeated builds reuse the existing cache and go straight to `cmake --build`, which lets CMake regenerate only when its tracked inputs change. The script keeps final executables under `build\`, preserves the repo-root `vcpkg\` manifest install tree across clean builds, restores `build\cmake\compile_commands.json` for `clangd`-based editors, and keeps vcpkg download plus registry caches in a user-local cache root so fresh worktrees reuse the same bootstrap downloads. The vcpkg manifest installs test dependencies plus the tree-sitter runtime used by native repository tools; `vcpkg-configuration.json` adds the project overlay port under `cmake\vcpkg-ports\` for package-managed grammar dependencies that are not available in the active curated registry. The tree-sitter C++ overlay applies the repository grammar patch, installs the upstream grammar npm dependencies without copying their sources, downloads the pinned tree-sitter CLI through vcpkg, and generates parser sources from `tools\format_config.json` macro categories. The default build targets are `CaseDash`, `CaseDashHeadless`, `CaseDashTests`, and `CaseDashTools`; `CaseDashBenchmarks` is built only when `build.cmd /benchmarks` or `build.cmd Release /benchmarks` is requested, and `build.cmd /tools` builds only `CaseDashTools` for native tooling refreshes. `CaseDashHeadless.exe` is a build, CI, and website asset tool; it is not installed or published as a release payload and is linked as an explicit console-subsystem executable with headless-specific manifest identity. CMake embeds the manifest that declares UTF-8 as the process active code page, and maintained native targets are built for explicit Win32 A API text boundaries rather than `UNICODE` macro dispatch. CMake generates config runtime metadata and a review manifest under `build\cmake\generated\config\` from `src\config\config.h` and `src\config\config_primitives.h`; it also generates layout-edit field metadata under `build\cmake\generated\layout_model\`. The config generator validates generated section and key spellings against `resources\config.ini`. CMake generates a compressed embedded text-resource atlas under `build\cmake\generated\compressed_resources\` for headless, tests, and tools, plus a production atlas under `build\cmake\generated\compressed_resources_app\` that excludes headless-only source strings; both atlases exclude the headless-only `[layout_guide_sheet]` config section. `CaseDashHeadless.exe` alone links a generated `RCDATA` resource containing that section and loads it as a diagnostics config extension before layout guide sheet export. Both atlases use the maintained config and localization source files in `resources\` plus deduplicated first source-use `RES_STR("...")` trace strings scanned from the selected source set. The resource generator validates embedded source text as BOM-free UTF-8, writes `build\cmake\generated\resource_strings.generated.h` with the collision-checked hash seed for compile-time `RES_STR` literals, and rewrites generated outputs only when their contents change.

By default the shared cache root is `%LOCALAPPDATA%\CaseDash\cache`, falls back to `%USERPROFILE%\.casedash\cache` when `LOCALAPPDATA` is unavailable, and can be overridden with `CASEDASH_CACHE_ROOT`. `build.cmd` exports `VCPKG_DOWNLOADS` and `X_VCPKG_REGISTRIES_CACHE` from that root unless the caller already set them.

If `build.cmd` must refresh CMake or vcpkg state without deleting the build tree, set `CASEDASH_FORCE_CONFIGURE=1` for that run. If `devenv.cmd` or the Visual Studio toolchain changes, delete `build\cmake` before the next build so CMake does not reuse stale compiler paths; the repo-root `vcpkg\` install tree and shared downloads cache are preserved.

Release linker maps are opt-in size-investigation artifacts and are not produced by the normal build. Run `build_maps.cmd` to configure `CASEDASH_LINK_MAPS=ON`, force the app executable to relink, write `build\CaseDash.map`, and write the maintained summary to `build\CaseDash.map.summary.txt`. Add `/benchmarks` only when benchmark maps are needed; that also writes `build\CaseDashBenchmarks.map` and `build\CaseDashBenchmarks.map.summary.txt`. For ad hoc inspection of an existing map, run `python tools\analyze_link_map.py build\CaseDash.map --top 25`; to compare two maps by inferred symbol delta, run `python tools\compare_link_maps.py build\CaseDash.map path\to\other\CaseDash.map --top 10`. The analyzer estimates symbol sizes from adjacent MSVC map addresses, so its object, library, and symbol rankings are investigation guides rather than exact byte ownership. The manual GitHub `Size Map Artifacts` workflow runs the same app map build on `windows-2025-vs2026` and uploads `CaseDash.exe`, `CaseDash.map`, `CaseDash.map.summary.txt`, and artifact metadata for remote toolchain comparisons. Size assumptions and active experiment guidance live in [docs/optimize_size.md](optimize_size.md).

## Test

Run unit tests after a successful build:

```bat
test.cmd
```

Diagnostics validation commands live in [docs/diagnostics.md](diagnostics.md).

## Package

Build the runtime and MSI package through the repository entrypoint:

```bat
package.cmd
```

`package.cmd` runs `build.cmd`, generates the WiX dialog and banner bitmaps under `build\installer_dialog_bmp\`, restores the WiX Toolset SDK through MSBuild, builds the x64 per-machine MSI from `installer\`, writes `build\CaseDash-<VERSION>.msi`, and writes the matching SHA-256 checksum. Local installation uses that MSI package. Release asset and installer behavior, including upgrade preservation of runtime-owned executable-side files, is maintained in [docs/release.md](release.md).

## Website

Build the static website through the repository entrypoint:

```bat
web-build.cmd
```

`web-build.cmd` runs `build.cmd`, reads themes from `resources\config.ini`, exports each theme's dashboard screenshot, layout guide sheet, and app icon through one `CaseDashHeadless.exe` diagnostics invocation when any generated asset for that theme is missing, writes theme metadata, and produces the directly openable static site under `web\dist\`. Generated website output is not committed. `web-build.cmd clean` deletes `web\dist\` first and forces all generated website assets to be rebuilt.

The `Release` workflow deploys the generated site after a successful tagged release. The manual-only `Pages` workflow rebuilds and deploys the generated site from a selected branch, tag, or commit without creating a release.

## Developer Tooling Entrypoints

- `format.cmd` is the maintained entrypoint for formatting non-vendored C++ sources. Its changed-file mode keeps Git CRLF normalization warnings out of formatter file discovery. `format.cmd --root <path> --file <path>` checks one eligible file relative to an alternate root, and `--stdout` with `--file` emits the project-normalized formatter result without writing the file so formatter fixtures can compare golden output. The formatter runs clang-format from `.clang-format` and then applies the project normalization pass in `tools\run_clang_format.ps1` for style constraints clang-format cannot express directly, including no padded ternary operator columns and leading indentation on four-space columns. Successful check and fix summaries report elapsed time. Mixed-mode C++/CLI bridge `.cpp` files are excluded because Visual Studio LLVM clang-format versions produce conflicting output for managed handles and `for each` syntax.
- `CaseDashTools.exe format` is an experimental native formatter subcommand for timing and evaluating a tree-sitter C++ alternative to the maintained `format.cmd` path. It scans non-vendored `.cpp` and `.h` files under `src\` and `tests\`, including mixed-mode C++/CLI provider bridges, supports `fix`, `changed`, `--root <path>`, `--file <path>`, `--stdout`, and `--verbose`, parses each file through the packaged tree-sitter C++ grammar, applies the formatter rules encoded in `src\tools\format.cpp`, and reports elapsed time plus the number of files whose tree-sitter parse contains errors. Project-specific macro names accepted by that grammar live in `tools\format_config.json`; formatting style rules remain hardcoded in the native formatter source. Its output is intentionally simple and not clang-format-compatible.
- `test.cmd` is the maintained entrypoint for running the CTest suite against the built Release tree, the clang-format regression fixture under `tools\tests\format`, and the native lint-check regression fixture under `tools\tests\lint`. It prints verbose CTest output so GitHub runner logs show each test command and its stdout even when tests pass.
- The repo `pre-commit` hook launches `tools\pre_commit_checks.ps1`, which formats staged eligible C++ files through `tools\run_clang_format.ps1` and runs `lint.cmd` before each commit. Git file discovery starts from all staged `.cpp` and `.h` paths, then the shared formatter filter limits work to maintained non-vendored `src\` and `tests\` files. The hook temporarily stashes unstaged and untracked files while it formats and lints the staged snapshot, restores them before exit, and aborts the commit when formatting or lint checks fail.
- `lint.cmd` is the maintained entrypoint for architecture checks, source dependency checks, include-path checks, no-local-`NOLINT` checks, header-body checks, renderer-only Direct2D boundary checks, and source-policy checks such as the project-wide `std::function`, `std::filesystem`, STL threading primitive, `std::hash`, and wide-literal bans. It runs `build\CaseDashTools.exe lint_check --config <repo>\tools\lint_config.json --check` from the repository root, building `CaseDashTools` first only when the executable is missing, tool source files are newer than the executable, or `CMakeLists.txt` is newer than the generated `build\cmake\build.ninja` graph. The native `lint_check` command supports `--config <path>`, `--check`, `--no-progress`, `--report-json <path>`, and `-v` or `--verbose`; graph-output arguments are not supported. `tools\lint_config.json` owns shared suffix groups, lint scan roots, checker eligibility, exclusions, include roots, allowlists, package dependency limits, and checker-owned source-policy or suppression rules. Clean runs print one total-LOC summary plus the timed success line; failing runs report findings plus the timed failure line. Pass `--report-json <path>` to write the same failures as a machine-readable JSON report with `schema_version`, `failed`, and `diagnostics` fields. Pass `-v` or `--verbose` to `lint.cmd` or `CaseDashTools.exe lint_check` for source files above the configured LOC threshold and DAG topological package dependency details. The source-policy rationale lives in [docs/source_policy_guardrails.md](source_policy_guardrails.md).
- `lint.cmd includes` runs the maintained clangd unused-include check for non-vendored `.cpp` and `.h` files under `src\` and `tests\`, excludes selected provider bridge files, writes `build\clang_include_cleaner_report.txt`, uses `.clangd` `Diagnostics.UnusedIncludes: Strict`, and line-filters clangd diagnostics to `#include` directives. `lint.cmd includes changed` narrows the same check to changed eligible files. The clangd include check reports unused-include diagnostics plus clangd parse errors and does not support automatic fixing. Interactive include-lint progress updates one line as files complete, and its counter tracks completed files rather than queued files.
- `tools\update_readme_images.ps1` is the maintained entrypoint for updating the committed README screenshots under `docs\image\`. It builds the app by default and exports the `dark_cyan` and `blueprint_light` screenshots from built-in synthetic telemetry with fixed `/scale:2` rendering. Pass `-SkipBuild` only when `build\CaseDash.exe` is already current.
- `tools\optimize_png_resources.py` losslessly recompresses committed PNG resources and PNG-backed ICO frames. `tools\update_app_icon.ps1` runs it after regenerating `resources\app.ico`.
- `package.cmd` is the maintained local entrypoint for producing the release MSI outside the GitHub Release workflow. It normalizes `major.minor` versions from `VERSION` to `major.minor.0` for Windows Installer product-version rules while keeping the output filename on the original `VERSION` text.
- `web-build.cmd` is the maintained local entrypoint for producing the generated static website under `web\dist\`.
- CMake enables MSVC warning C4505 and treats it as an error so unreferenced internal functions are caught during normal builds when MSVC can diagnose them.
- Native C++ targets compile with `/GR-`; production code uses explicit project type tags instead of native RTTI. C++/CLI provider bridges keep managed casts in their `/clr` translation units.
- Release app and benchmark builds compile size-oriented code with `/Os` and `/GL`, then link with `/LTCG`, `/OPT:REF`, and non-incremental linking so whole-program optimization and reference elimination reduce the shipped executable while benchmarks measure the same optimization profile. The shipped Release app also links with `/DYNAMICBASE:NO` as an explicit executable-size tradeoff tracked in [docs/optimize_size.md](optimize_size.md). Native app code and the C++/CLI bridge targets define `_HAS_EXCEPTIONS=0`; managed bridge `try`/`catch` handling remains enabled for hardware-provider interop. Benchmark-sensitive renderer, widget, layout, telemetry, and benchmark-harness translation units retain `/O2` inside that Release profile so size work does not distort the maintained performance loops. Tests keep the normal Release compile/link path for faster local validation. The benchmark target is opt-in through `build.cmd /benchmarks`; `profile_benchmark.cmd` requests that target when it needs the benchmark executable.
- `profile_benchmark.cmd` is the maintained entrypoint for elevated benchmark profiling and daemon-backed benchmark requests. Daemon-backed requests write the ETL, xperf detail summary, process-filtered call tree, hotspot summary, and benchmark stdout under `build\profile_benchmark_daemon\requests\`.
- `devenv.cmd` is the maintained environment bootstrap for local builds and tool runs. GitHub Actions does not use this machine-local script; `build.cmd`, `format.cmd`, and `lint.cmd includes` resolve Visual Studio and LLVM tools from the runner environment.

## GitHub Validation

- The `Validation` workflow runs on pull requests targeting `main`, pushes to `main`, and manual dispatch. Feature-branch changes are validated through pull requests rather than duplicate branch-push runs.
- GitHub workflows restore the shared vcpkg download and registry caches under `.github-cache\CaseDash` inside the checked-out workspace before validation, then save the refreshed cache contents after the run so repeated GitHub-hosted runs reuse the same bootstrap downloads. Cache restore and save failures are best-effort and do not block validation.
- The workflow checks formatting first with `format.cmd`, then builds with `build.cmd /benchmarks`, runs tests with `test.cmd`, runs a `CaseDashHeadless.exe` diagnostics export smoke test, builds the WiX MSI with `package.cmd`, and runs `lint.cmd includes` on `windows-2025-vs2026`.
- The repository branch protection requires the `Validation` job before pull requests can merge.
- The workflow uploads `build\CaseDash.exe` as the `CaseDash-exe` artifact after validation succeeds.
- The workflow uploads `build\CaseDash-<VERSION>.msi` and its checksum as the `CaseDash-msi` artifact after validation succeeds.
- The workflow uploads `build\clang_include_cleaner_report.txt` as an artifact when it is produced.
- The manual-only `Size Map Artifacts` workflow builds through `build_maps.cmd` without tests or packaging, then uploads `CaseDash-size-map-exe` and `CaseDash-size-map` artifacts for executable-size and linker-map comparison across runner toolchains.

## Releases

- [docs/release.md](release.md) owns the official release workflow.
- `VERSION` is the maintained base version used by CMake-generated build metadata.
- Tagged release builds use tags in the form `v<VERSION>`, such as `v0.1`.
- The `Release` workflow validates the tag, publishes notes from the top [docs/changelog.md](changelog.md) chunk, builds CaseDash with benchmarks, runs tests, packages `CaseDash.exe`, builds the WiX MSI, writes SHA-256 checksums, and creates the GitHub Release.

