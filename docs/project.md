# System Telemetry Project Notes

This document owns documentation ownership rules, repository conventions, and engineering constraints.
See also: [docs/build.md](build.md) for setup and commands, [docs/layout.md](layout.md) for config language, [docs/diagnostics.md](diagnostics.md) for diagnostics behavior, and [docs/architecture.md](architecture.md) for code structure.

## Documentation Ownership

- `docs/specifications.md` owns user-visible runtime behavior.
- `docs/layout.md` owns config language, section ownership, syntax, and validation rules.
- `docs/diagnostics.md` owns diagnostics CLI behavior, output contracts, and diagnostics validation recipes.
- `docs/build.md` owns build prerequisites, developer commands, install flow, and tooling entrypoints.
- `docs/architecture.md` owns subsystem structure, code boundaries, runtime flows, and build-graph shape.
- `docs/profile_benchmark.md` owns benchmark workflow, baselines, hotspots, and experiment history.
- `resources/config.ini` is the maintained example and spelling authority for shipped config entries.
- `CMakeLists.txt` is the single maintained source of truth for native source lists, link libraries, and output-directory rules.
- `.clang-format` is the single maintained source of truth for C++ formatting policy.

## Repository Conventions

- Keep production sources in `src`, tests in `tests`, documentation in `docs`, and embedded assets in `resources`.
- Keep generated build outputs inside `build\`, with the repo-root `vcpkg\` directory as the deliberate persistent exception for manifest-installed dependencies.
- Keep tracked text files checked out with CRLF line endings through the repo-level `.gitattributes` policy; binary assets are excluded from text normalization there.
- Keep project-authored quoted includes rooted at the configured `src` and `resources` include directories.
- Keep C++ includes ordered by `format.cmd`: matching `.cpp` header first, then sorted angle/system includes, quoted `vendor/` includes, and sorted quoted project includes; WinSock and `windows.h` stay ahead of dependent Win32 headers.
- Keep Win32 hygiene macros such as `NOMINMAX` and `WIN32_LEAN_AND_MEAN` in target compile definitions instead of local include preambles.
- Keep each project `.cpp` paired with a matching header that owns its out-of-line declarations; `src/main/main.cpp` is the only headerless translation unit.
- Keep third-party source vendoring narrow and prefer package-managed dependencies where practical.

## Engineering Constraints

- Keep `resources/config.ini` as the embedded default configuration resource.
- Keep `resources/SystemTelemetry.rc` explicitly dependent on embedded payload files such as `resources/config.ini` and `resources/localization.ini`.
- Do not add C++-side synthesized fallback layout, card, widget, font, color, or styling defaults that duplicate the embedded template.
- Keep runtime text internally as UTF-8 `std::string` and convert to UTF-16 only at Windows API boundaries.
- Keep config-file I/O on standard C++ streams and preserve strict UTF-8 handling without ANSI code-page fallback.
- Keep `resources/localization.ini` as the embedded key-value catalog for localizable runtime strings.
- Keep one full-fidelity widget draw path for normal repaints and layout-edit drag repaints.
- When a private module needs shared mutable collector data across multiple `.cpp` files, expose that state through a dedicated state type plus module-owned free functions.

## Single-Source References

- Use [docs/build.md](build.md) instead of repeating build or install commands elsewhere.
- Use [docs/diagnostics.md](diagnostics.md) instead of repeating diagnostics command examples elsewhere.
- Use [docs/layout.md](layout.md) and [resources/config.ini](../resources/config.ini) instead of repeating config key lists elsewhere.
- Use [docs/profile_benchmark.md](profile_benchmark.md) instead of repeating benchmark workflow or experiment history elsewhere.
