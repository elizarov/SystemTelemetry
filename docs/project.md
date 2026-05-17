# CaseDash Project Notes

This document owns documentation ownership rules, repository conventions, and engineering constraints.
See also: [docs/build.md](build.md) for setup and commands, [docs/layout.md](layout.md) for config language, [docs/diagnostics.md](diagnostics.md) for diagnostics behavior, [docs/release.md](release.md) for release publication, [docs/optimize_size.md](optimize_size.md) for executable-size research, and [docs/architecture.md](architecture.md) for code structure.

## Documentation Ownership

- `docs/specifications.md` owns general user-visible runtime behavior.
- `docs/animation.md` owns live-dashboard animation behavior, target render-thread architecture, and animation-specific implementation contracts.
- `docs/hardware.md` owns supported hardware-provider details, provider runtime requirements, provider-specific telemetry behavior, and provider troubleshooting.
- `docs/layout_edit.md` owns layout-edit mode, edit-target interaction, modeless editor behavior, and layout-edit save or discard behavior.
- `docs/layout.md` owns config language, section ownership, syntax, and validation rules.
- `docs/layout_guide_sheet.md` owns the diagnostics layout guide sheet feature spec.
- `docs/diagnostics.md` owns diagnostics CLI behavior, output contracts, and diagnostics validation recipes.
- `docs/build.md` owns build prerequisites, developer commands, install flow, and tooling entrypoints.
- `docs/source_policy_guardrails.md` owns short explanations for hard source-policy lessons, including lint-enforced bans and review-enforced rules that are too noisy to check mechanically.
- `docs/architecture.md` and `docs/architecture/*.md` own subsystem structure, code boundaries, package notes, runtime flows, and build-graph shape.
- `docs/web.md` owns public website behavior, content, generated-asset contracts, and website build flow.
- `docs/profile_benchmark.md` owns benchmark workflow, baselines, hotspots, and performance experiment history.
- `docs/optimize_size.md` owns executable-size constraints, assumptions, map workflow notes, and size experiment history.
- `docs/release.md` owns release versioning, changelog format, local release preparation, and release publication.
- `docs/changelog.md` owns machine-readable release-note chunks consumed by the local release script and GitHub Release workflow.
- `resources/config.ini` is the maintained example and spelling authority for shipped config entries.
- `CMakeLists.txt` is the single maintained source of truth for native source lists, link libraries, and output-directory rules.
- `installer\` is the single maintained source of truth for the WiX MSI package.
- `web\` is the single maintained source of truth for the static website source and website build script.
- `.clang-format` is the single maintained source of truth for C++ formatting policy. `format.cmd` owns narrow exclusions for mixed-mode C++/CLI bridge `.cpp` files that clang-format versions do not format consistently.
- `.github/workflows/validation.yml` is the single maintained source of truth for branch push and pull request build, test, format, lint, and tidy automation.
- `.github/workflows/size-map-artifacts.yml` is the single maintained source of truth for manually producing remote executable and linker-map artifacts for size investigation.
- `.github/workflows/release.yml` and `.github/workflows/pages.yml` are the single maintained sources of truth for website deployment automation.

## Repository Conventions

- Keep production sources in `src`, tests in `tests`, documentation in `docs`, and embedded assets in `resources`.
- Keep reusable agent or automation skills in `.agents\skills`.
- Use `tools\update_app_icon.ps1` to rebuild the app, export compressed default-theme app-icon PNGs under `build\app_icon\`, regenerate `resources\app.ico` from those rendered assets, and losslessly recompress the ICO's PNG frames.
- `package.cmd` generates the WiX dialog and banner bitmaps under `build\installer_dialog_bmp\` from native-size dark_cyan app icon exports before building the MSI; generated installer bitmaps are not committed.
- Use `tools\update_readme_images.ps1` to update committed README screenshots under `docs\image\`.
- Use `tools\generate_social_preview.ps1` to generate the GitHub social preview PNG under `build\social_preview\` from dark_cyan fake telemetry and a rendered app icon.
- Keep generated build outputs inside `build\`, with `web\dist\` as the generated website output and the repo-root `vcpkg\` directory as the deliberate persistent exception for manifest-installed dependencies.
- Keep shared vcpkg download and registry caches outside the worktree in the user-local cache root that `build.cmd` exports through `VCPKG_DOWNLOADS` and `X_VCPKG_REGISTRIES_CACHE`.
- Keep GitHub-restored dependency caches under `.github-cache\`, which is ignored and owned by the GitHub workflows.
- Keep pull request merge protection tied to the GitHub `Validation` job so pushed changes pass build, test, formatting, and tidy checks on the Windows runner before merge.
- Keep tracked text files checked out with CRLF line endings through the repo-level `.gitattributes` policy; binary assets are excluded from text normalization there.
- Keep project-authored quoted includes rooted at the configured `src` and `resources` include directories.
- Keep local `NOLINT` suppressions out of source files; maintained clang-tidy false positives live in the lint tool allowlist.
- Keep C++ includes ordered by `format.cmd`: matching `.cpp` header first, then sorted angle/system includes, quoted `vendor/` includes, and sorted quoted project includes; WinSock and `windows.h` stay ahead of dependent Win32 headers.
- Keep Win32 hygiene macros such as `NOMINMAX` and `WIN32_LEAN_AND_MEAN` in target compile definitions instead of local include preambles.
- Keep each project `.cpp` paired with a matching header that owns its out-of-line declarations; `src/main/main.cpp` is the only headerless translation unit.
- Keep third-party source vendoring narrow and prefer package-managed dependencies where practical.

## Engineering Constraints

- Keep `resources/config.ini` as the embedded default configuration resource.
- Keep committed resource payloads as the source of truth; CMake generates the compressed embedded config/localization text atlas under `build\cmake\generated\`, while `resources/CaseDash.rc` keeps the directly embedded app icon and panel-icon mask atlas explicit.
- Keep `VERSION` as the single maintained base product version; generated headers, manifests, and version resources derive their build metadata from it plus Git state.
- Do not add C++-side synthesized fallback layout, card, widget, font, color, or styling defaults that duplicate the embedded template.
- Keep runtime text internally as UTF-8 `std::string` and convert to UTF-16 only at Windows API boundaries.
- Keep source string constants as narrow UTF-8 literals by default. `lint.cmd` blocks undocumented wide literals in maintained source and test files; only `const` or `constexpr wchar_t` string constants with an end-of-line reason comment are allowed for fixed Win32 or managed interop boundary text.
- Keep shared string formatting on trace formatting APIs, `src/util/text_format.*`, and domain-owned utility helpers instead of repeating local `std::to_string` concatenation or append-builder chains.
- Keep config-file I/O on standard C++ streams and preserve strict UTF-8 handling without ANSI code-page fallback.
- Keep project filesystem operations on `src/util/file_path.*` helpers instead of `std::filesystem`; paths are stored as UTF-8 and widened only at filesystem API calls. `lint.cmd` enforces this source-policy rule for maintained source and test files.
- Keep native app and benchmark targets built without native C++ exception handling; the C++/CLI bridge owns the managed exception boundary separately.
- Keep native app, test, and benchmark targets compiled with `_USE_STD_VECTOR_ALGORITHMS=0` so MSVC's vectorized STL algorithm dispatch tables stay out of the single-file binaries.
- Keep `resources/localization.ini` as the embedded key-value catalog for localizable runtime strings.
- Keep one full-fidelity widget draw path for normal repaints and layout-edit drag repaints.
- When a private module needs shared mutable collector data across multiple `.cpp` files, expose that state through a dedicated state type plus module-owned free functions.

## Single-Source References

- Use [docs/build.md](build.md) instead of repeating build or install commands elsewhere.
- Use [docs/diagnostics.md](diagnostics.md) instead of repeating diagnostics command examples elsewhere.
- Use [docs/layout.md](layout.md) and [resources/config.ini](../resources/config.ini) instead of repeating config key lists elsewhere.
- Use [docs/profile_benchmark.md](profile_benchmark.md) instead of repeating benchmark workflow or experiment history elsewhere.
- Use [docs/optimize_size.md](optimize_size.md) instead of repeating executable-size assumptions, constraints, or experiment history elsewhere.
- Use [docs/release.md](release.md) and [docs/changelog.md](changelog.md) instead of repeating release procedure or changelog format details elsewhere.

## Project Pitfall Notes

- Fake-runtime startup failures stay aligned with the diagnostics dialog policy; direct modal dialogs in `/fake /exit` can make a headless process wait behind the dialog.
- Win32 dialog templates and control ids live in `resources/CaseDash.rc` and `resources/resource.h`; check those files when shell dialog layout or control placement is wrong.
- The executable-side `config.ini` overlays the embedded `resources/config.ini` template, and `Save Config` preserves that live file.
- Embedded `config.ini` and `localization.ini` edits flow through the generated BOM-free UTF-8 text-resource atlas; app-icon and panel-icon edits depend on explicit `resources/CaseDash.rc` CMake dependencies so incremental builds rebuild the resource object.
- The generated compressed-resource RC object depends explicitly on `text_atlas.cdlz`; keep that dependency when changing generated text-resource outputs because RC compilation does not otherwise track RCDATA payload changes.
- Restored saved placement across monitors with different DPI scales lets `WM_DPICHANGED` apply the monitor transition before destination window size scaling.
- Mouse-driven visual feedback cannot rely only on `WM_TIMER` or queued `WM_PAINT`; continuous pointer input can starve low-priority messages, so move mode and active drags update or redraw from processed pointer messages.
- Login startup and monitor hotplug can race monitor enumeration; `display.monitor_name` placement keeps watching until the target display becomes enumerable.
- Provider assembly loading restores the original launch working directory after any provider-specific current-directory change.
- Repeated unattended profiling runs use `profile_benchmark.cmd /daemon-start` once, then ordinary benchmark invocations queue through the elevated daemon.
- Failed or regressed benchmark optimization experiments are recorded in `docs/profile_benchmark.md`; size-specific experiments are recorded in `docs/optimize_size.md`.
- If `devenv.cmd` changes Visual Studio toolchains, delete `build\cmake` before the next `build.cmd` run.
- Formatter and hook discovery starts from broad `*.cpp` and `*.h` pathspecs, then applies the repo eligibility filter because Git pathspecs such as `tests/**/*.cpp` do not cover top-level files.
- Clang-tidy include-cleaner false-positive filters stay narrow so Win32 umbrella headers and project macro-provider headers do not hide real unused includes.
- GitHub Actions does not call machine-local `devenv.cmd`; CI resolves Visual Studio through the runner environment and sets `CASEDASH_TIDY_TIMEOUT_SECONDS` for the tidy sweep.
- `for /f` commands invoke `vswhere.exe` through `call "%VSWHERE%" ...` so `cmd` does not try to execute `C:\Program`.
- Config-schema reflection descriptors stay type-derived and default-initialized because the GitHub Visual Studio runner can lag the local MSVC toolset.
- The repo uses CRLF text checkouts; `.githooks/pre-commit` stays a minimal CRLF-tolerant shell launcher, and multi-line hook logic lives in PowerShell.
- The installer completion launch stays directory-sourced instead of `FileRef`-sourced so the Finish button does not depend on File table action state and surface MSI error 2753.
