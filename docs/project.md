# System Telemetry project notes

## Project environment

- The project runs on Windows 11 and is implemented in C++20 using the Visual Studio 2026 Insiders (`18`) MSVC toolchain.
- Use `devenv.cmd` to activate the development environment when needed.
- Keep machine-specific compiler/tool paths in `devenv.cmd`, and document the expected `devenv.cmd` contract in `devenv.md`.
- Keep `docs/build.md` as the single maintained source of truth for build prerequisites, build invocation, and developer-facing setup notes; other docs should link to it and avoid repeating that material.
- Keep production sources in `src`, unit tests in `tests`, and package-managed test dependencies declared through the repository manifest.
- Keep generated build outputs inside `build\` so the project root stays clean.
- Keep the repo-root `vcpkg\` directory as the only maintained exception to the `build\`-only generated-output rule; it holds the manifest-installed dependency tree so clean builds can delete `build\` without forcing vcpkg to restore packages again.
- Keep the CMake build tree under `build\cmake\` so generated project files, dependency state, and object directories stay inside `build\` as well.
- Keep the CMake build tree on the `Ninja Multi-Config` generator so `build\cmake\compile_commands.json` stays available for `clangd`-based editors such as Zed.
- When `build.cmd` runs under the Codex sandbox, route ephemeral compiler and linker scratch space through a fresh per-build subdirectory under the user's temp directory so MSVC tools can delete their own temp files successfully.
- Keep transient toolchain scratch files out of the top level of `build\` so the main build output folder stays readable.
- Keep `build.cmd` using the same active developer environment for both CMake and vcpkg resolution so the manifest install and configure step stay on one Visual Studio toolchain.
- If `devenv.cmd` changes to a different Visual Studio toolchain, delete `build\cmake\` before the next `build.cmd` run so CMake, the active compiler, and the vcpkg-detected compiler do not keep mixing different MSVC and STL versions.
- Keep `build.cmd` pointing `VCPKG_INSTALLED_DIR` at the repo-root `vcpkg\` directory so manifest-managed test dependencies survive `build\` deletion.
- Always build through `build.cmd`.
- Use the top-level `profile_benchmark.cmd` script as the maintained entry point for benchmark CPU profiling; it writes the benchmark ETL, flat CPU summary, and benchmark-only butterfly call tree HTML export into `build\`, takes the benchmark name as its first positional argument before optional `iterations` and `render_scale`, accepts `/daemon-start`, `/daemon-stop`, and `/daemon-status` to manage one elevated benchmark daemon under `build\profile_benchmark_daemon\`, hands off ordinary benchmark runs to that daemon when it is ready, rebuilds the `Release` benchmark binaries inside that elevated daemon before each queued run, resolves `xperf.exe` from the installed Windows Performance Toolkit at runtime, and otherwise accepts `/elevate` to relaunch a one-shot elevated run when WPR profiling needs `SeSystemProfilePrivilege`.
- Keep the repo-level `.clang-format` as the single maintained source of truth for C++ formatting rules.
- Keep the repo-level `.clangd` as the maintained source of truth for the project compilation-database path used by `clangd` clients.
- Use the top-level `format.cmd` script as the maintained entry point for formatting non-vendored C++ sources; run `format` to check the full tracked-plus-untracked project C++ file set, `format fix` to apply the repo style to that same full set, and `format changed` or `format fix changed` to target only changed project `.cpp` and `.h` files under `src\` and `tests\` while still skipping deleted paths and `src\vendor\`, and resolve `clang-format.exe` from the active developer environment.
- Keep the repo-local `.githooks\pre-commit` hook configured through `core.hooksPath` so each commit automatically runs the maintained formatter against staged project C++ files, restages those formatting edits, and preserves any unstaged or untracked work by stashing it around the hook run.
- Use the top-level `lint.cmd` script as the maintained entry point for header-to-implementation ownership checks, matching-header checks for non-allowlisted project `.cpp` files, project include-path style checks, and header-body checks; `lint` runs the architectural policy and include-style checks and `lint tidy` runs the optional `clang-tidy` sweep through `tools/run_clang_tidy.ps1`, supports both the full-translation-unit sweep and the faster `lint tidy changed` mode for changed project `.cpp` files, discovers project sources under `src\` and `tests\` while skipping `src\vendor\`, `src\board_gigabyte_siv.cpp`, and the maintained tidy skip-list entries such as `src\snapshot_dump.cpp` when Clang analysis makes those translation units impractically slow, resolves `clang-tidy.exe` from the active developer environment or the compiler recorded in `build\cmake\compile_commands.json` so the analyzer matches the current MSVC STL, includes the targeted `misc-include-cleaner` check for unused includes in project translation units, runs translation units in parallel while appending each completed file's output back into `build\clang_tidy_report.txt` immediately in completion order so report sections do not overlap, records per-file elapsed seconds in that report, and keeps per-translation-unit temp stdout/stderr logs under `build\clang_tidy_units\`.
- Keep `CMakeLists.txt` as the single maintained source of truth for native source lists, link libraries, and output-directory rules, with parallel scripts referring back to that build graph.
- Keep the focused performance benchmark host in the native build as `SystemTelemetryBenchmarks` so both the `edit-layout` drag path and the `update-telemetry` steady-state snapshot-refresh path are measured against the real controller, config-apply, live telemetry collector update, and renderer paint flow.
- Keep the `Release` benchmark link configured to emit `build\SystemTelemetryBenchmarks.pdb` so Windows Performance Toolkit exports can resolve benchmark CPU samples to project function names.
- Provide an `install.cmd` script at the repository root that requests elevation, stops running `SystemTelemetry.exe` instances before deployment, waits for those processes to exit, installs the already-built runtime into `C:\Program Files\SystemTelemetry`, copies `build\SystemTelemetry.exe` there, and leaves auto-start registration to the runtime popup-menu toggle.
- Keep third-party source vendoring narrow; prefer package-managed dependencies where practical and keep only the minimal ADLX SDK subset needed for the AMD GPU provider in-tree.

## Repository conventions

- Keep the repository license text in the top-level `LICENSE` file so hosting platforms can detect the project license from the standard path.
- `docs/specifications.md` should keep only the core user-visible dashboard behavior requirements that are not diagnostics-specific.
- `docs/diagnostics.md` should capture command-line diagnostics behavior, trace/dump/screenshot output requirements, and diagnostics-specific verification expectations.
- `docs/layout.md` should capture the configuration language syntax, section ownership, and maintained inline language examples.
- `docs/project.md` should capture project environment, build/setup expectations, and other engineering constraints that are not direct user-visible behavior.
- `docs/architecture.md` should capture structural and code-organization details.
- `.clang-format` should capture the maintained C++ formatting policy, and docs or scripts should refer to it.

## Implementation constraints

- Keep the checked-in config template at `resources/config.ini` so it can be embedded into the executable as the default configuration resource.
- Keep `resources/SystemTelemetry.rc` explicitly dependent on embedded payload files such as `resources/config.ini` and `resources/localization.ini` so incremental builds refresh the executable resources when those UTF-8 source files change.
- Keep project-authored quoted includes rooted at the configured `src` and `resources` include directories, so headers under nested source folders are included as `layout_edit_dialog/editors.h` or `widget/gauge.h`.
- Keep each non-allowlisted project `.cpp` paired with a matching header, and keep its out-of-line declarations owned by that header; the architecture lint matches fully qualified owners to the header that defines the owning class or function.
- Treat `resources/config.ini` as the single maintained source of truth for config-file entry documentation, and have other docs refer to it.
- Do not add C++-side synthesized fallback layout/card/widget defaults that duplicate the embedded config template; shipped defaults must come only from `resources/config.ini`.
- Do not keep code-side fallback fonts, colors, or layout-size defaults in the config structs; shipped UI styling defaults must come only from `resources/config.ini`.
- All runtime text such as config values, telemetry names, diagnostics, monitor identifiers, and UI strings must be stored internally as UTF-8 `std::string`.
- Convert between UTF-8 `std::string` and UTF-16 `std::wstring` only at Windows API boundaries.
- Configuration file I/O must use standard C++ stream primitives and read/write UTF-8 text directly without introducing a BOM during saves.
- Keep `resources/config.ini`, `resources/localization.ini`, and executable-side `config.ini` strictly UTF-8; do not add ANSI code-page fallbacks when decoding text.
- Keep the embedded `resources/config.ini` template concise and mostly self-documenting; prefer short section-divider comments over long per-key comment blocks.
- Keep localizable runtime strings in the embedded `resources/localization.ini` UTF-8 key=value catalog so string keys remain stable without assigning Win32 string-table ids and one file stays ready for translation work.
- The executable-side `config.ini` overlays the embedded template on load, `Save Config` updates only changed live values in that file, and `Save Full Config To...` exports the full embedded-template-shaped text.
- Keep one full-fidelity widget draw path as the only maintained renderer behavior for both ordinary repaints and layout-edit drag repaints; do not add drag-preview-specific draw shortcuts that change which visual work is executed during active drags.
- When a private module needs shared mutable collector data across multiple `.cpp` files, expose that data as a dedicated state type plus module-owned free functions.
