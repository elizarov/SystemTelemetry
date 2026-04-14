# System Telemetry project notes

## Project environment

- The project runs on Windows 11 and is implemented in C++20 using Visual Studio 2022.
- Use `devenv.cmd` to activate the development environment when needed.
- Keep machine-specific compiler/tool paths in `devenv.cmd`, and document the expected `devenv.cmd` contract in `devenv.md`.
- Keep `docs/build.md` as the single maintained source of truth for build prerequisites, build invocation, and developer-facing setup notes; other docs should link to it instead of repeating that material.
- Keep production sources in `src`, unit tests in `tests`, and package-managed test dependencies declared through the repository manifest instead of vendored into the source tree.
- Keep generated build outputs inside `build\` so the project root stays clean.
- Keep the CMake build tree under `build\cmake\` so generated project files, dependency state, and object directories stay inside `build\` as well.
- Keep the CMake build tree on the `Ninja Multi-Config` generator so `build\cmake\compile_commands.json` stays available for `clangd`-based editors such as Zed.
- When `build.cmd` runs under the Codex sandbox, route ephemeral compiler and linker scratch space through a fresh per-build subdirectory under the user's temp directory instead of `build\` so MSVC tools can delete their own temp files successfully.
- Keep transient toolchain scratch files out of the top level of `build\` so the main build output folder stays readable.
- Always build through `build.cmd`.
- Use the top-level `profile_benchmark.cmd` script as the maintained entry point for benchmark CPU profiling; it writes the benchmark ETL, flat CPU summary, and benchmark-only butterfly call tree HTML export into `build\`, accepts optional `iterations` and `render_scale` positional arguments, accepts `/daemon-start`, `/daemon-stop`, and `/daemon-status` to manage one elevated benchmark daemon under `build\profile_benchmark_daemon\`, hands off ordinary benchmark runs to that daemon when it is ready, rebuilds the `Release` benchmark binaries inside that elevated daemon before each queued run, and otherwise accepts `/elevate` to relaunch a one-shot elevated run when WPR profiling needs `SeSystemProfilePrivilege`.
- Keep the repo-level `.clang-format` as the single maintained source of truth for C++ formatting rules.
- Keep the repo-level `.clangd` as the maintained source of truth for the project compilation-database path used by `clangd` clients.
- Use the top-level `format.cmd` script as the maintained entry point for formatting non-vendored C++ sources; run `format` to check and `format fix` to apply the repo style.
- Use the top-level `lint.cmd` script as the maintained entry point for header-to-implementation ownership checks and header-body checks; `lint` runs the architectural policy checks and `lint tidy` runs the optional whole-repo `clang-tidy` sweep.
- Keep `CMakeLists.txt` as the single maintained source of truth for native source lists, link libraries, and output-directory rules instead of duplicating that build graph in parallel scripts.
- Keep the focused layout-edit performance benchmark in the native build as `SystemTelemetryBenchmarks` so drag-path regressions can be measured against the real controller, config-apply, snap-evaluation, and renderer paint flow instead of against a synthetic relayout-only loop.
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
- `.clang-format` should capture the maintained C++ formatting policy instead of duplicating style rules across docs or scripts.

## Implementation constraints

- Keep the checked-in config template at `resources/config.ini` so it can be embedded into the executable as the default configuration resource.
- Keep `resources/SystemTelemetry.rc` explicitly dependent on embedded payload files such as `resources/config.ini` and `resources/localization.ini` so incremental builds refresh the executable resources when those UTF-8 source files change.
- Treat `resources/config.ini` as the single maintained source of truth for config-file entry documentation instead of duplicating that format documentation elsewhere in the repo.
- Do not add C++-side synthesized fallback layout/card/widget defaults that duplicate the embedded config template; shipped defaults must come only from `resources/config.ini`.
- Do not keep code-side fallback fonts, colors, or layout-size defaults in the config structs; shipped UI styling defaults must come only from `resources/config.ini`.
- All runtime text such as config values, telemetry names, diagnostics, monitor identifiers, and UI strings must be stored internally as UTF-8 `std::string`.
- Convert between UTF-8 `std::string` and UTF-16 `std::wstring` only at Windows API boundaries.
- Configuration file I/O must use standard C++ stream primitives and read/write UTF-8 text directly without introducing a BOM during saves.
- Keep the embedded `resources/config.ini` template concise and mostly self-documenting; prefer short section-divider comments over long per-key comment blocks.
- Keep localizable runtime strings in the embedded `resources/localization.ini` UTF-8 key=value catalog so string keys remain stable without assigning Win32 string-table ids and one file stays ready for translation work.
- The executable-side `config.ini` overlays the embedded template on load, `Save Config` updates only changed live values in that file, and `Save Full Config To...` exports the full embedded-template-shaped text.
- Keep headers declarative: non-template and non-inline-required production logic belongs in `.cpp` files, not in project headers.
- Keep one full-fidelity widget draw path as the only maintained renderer behavior for both ordinary repaints and layout-edit drag repaints; do not add drag-preview-specific draw shortcuts that change which visual work is executed during active drags.
