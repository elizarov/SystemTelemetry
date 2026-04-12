# System Telemetry project notes

## Project environment

- The project runs on Windows 11 and is implemented in C++20 using Visual Studio 2022.
- Use `devenv.cmd` to activate the development environment when needed.
- Keep machine-specific compiler/tool paths in `devenv.cmd`, and document the expected `devenv.cmd` contract in `devenv.md`.
- Keep `docs/build.md` as the single maintained source of truth for build prerequisites, build invocation, and developer-facing setup notes; other docs should link to it instead of repeating that material.
- Keep production sources in `src` and unit tests plus vendored test support in `tests`.
- Keep generated build outputs inside `build\` so the project root stays clean.
- Keep the CMake build tree under `build\cmake\` so generated project files, dependency state, and object directories stay inside `build\` as well.
- When `build.cmd` runs under the Codex sandbox, route ephemeral compiler and linker scratch space through a fresh per-build subdirectory under the user's temp directory instead of `build\` so MSVC tools can delete their own temp files successfully.
- Keep transient toolchain scratch files out of the top level of `build\` so the main build output folder stays readable.
- Always build through `build.cmd`.
- Keep the repo-level `.clang-format` as the single maintained source of truth for C++ formatting rules.
- Use the top-level `format.cmd` script as the maintained entry point for formatting non-vendored C++ sources; run `format` to check and `format fix` to apply the repo style.
- Keep `CMakeLists.txt` as the single maintained source of truth for native source lists, link libraries, and output-directory rules instead of duplicating that build graph in parallel scripts.
- Provide an `install.cmd` script at the repository root that requests elevation, stops running `SystemTelemetry.exe` instances before deployment, installs the already-built runtime into `C:\Program Files\SystemTelemetry`, copies `build\SystemTelemetry.exe` there, and leaves auto-start registration to the runtime popup-menu toggle.

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
- Treat `resources/config.ini` as the single maintained source of truth for config-file entry documentation instead of duplicating that format documentation elsewhere in the repo.
- Do not add C++-side synthesized fallback layout/card/widget defaults that duplicate the embedded config template; shipped defaults must come only from `resources/config.ini`.
- Do not keep code-side fallback fonts, colors, or layout-size defaults in the config structs; shipped UI styling defaults must come only from `resources/config.ini`.
- All runtime text such as config values, telemetry names, diagnostics, monitor identifiers, and UI strings must be stored internally as UTF-8 `std::string`.
- Convert between UTF-8 `std::string` and UTF-16 `std::wstring` only at Windows API boundaries.
- Configuration file I/O must use standard C++ stream primitives and read/write UTF-8 text directly without introducing a BOM during saves.
- Keep the embedded `resources/config.ini` template concise and mostly self-documenting; prefer short section-divider comments over long per-key comment blocks.
- The executable-side `config.ini` overlays the embedded template on load, `Save Config` updates only changed live values in that file, and `Save Full Config To...` exports the full embedded-template-shaped text.
- Keep headers declarative: non-template and non-inline-required production logic belongs in `.cpp` files, not in project headers.
