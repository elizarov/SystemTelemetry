# Size Optimization Log

This document owns executable-size assumptions, constraints, map workflow notes, and size experiment history. Keep benchmark timing details in [docs/profile_benchmark.md](profile_benchmark.md).

## Constraints

- Optimize `build\CaseDash.exe`; benchmark binary size is not a release goal.
- Keep CaseDash as one executable with the full feature set. Do not split payloads into side files or remove supported hardware, diagnostics, layout-edit, localization, or default-config behavior.
- Preserve runtime performance. Prefer cold-path, resource, metadata, and linker-shape wins before touching renderer, widget draw, layout resolver, or telemetry hot paths.
- Keep embedded `resources/config.ini` and `resources/localization.ini` available through the executable.
- Keep normal builds free of map generation. Use `build_maps.cmd` for app maps and `build_maps.cmd /benchmarks` only when benchmark maps matter.
- Write map summaries under `build\`; do not rely on console-only analysis output.
- Treat MSVC map symbol sizes as estimates inferred from adjacent addresses.

## Current State

- Current measured `build\CaseDash.exe`: `1,326,592` bytes.
- Current app map summary: `build\CaseDash.map.summary.txt`.
- Current largest sections: `.text$mn` about `1.07 MiB`, `.rdata` about `87.4 KiB`, `.rsrc$02` about `34.5 KiB`, `.pdata` about `24.0 KiB`, `.xdata` about `21.6 KiB`.
- Current largest project objects: `diagnostics.cpp.obj`, `editors.cpp.obj`, `dashboard_app.cpp.obj`, `dashboard_controller.cpp.obj`, `layout_guide_sheet_renderer.cpp.obj`, `dashboard_shell_ui.cpp.obj`, and `layout_resolver.cpp.obj`.

## Workflow

- Build the app normally with `build.cmd`.
- Generate the app map and summary with `build_maps.cmd`.
- Inspect the maintained summary at `build\CaseDash.map.summary.txt`.
- For ad hoc map inspection, run `python tools\analyze_link_map.py build\CaseDash.map --top 25`.
- When a size change can affect hot code, build benchmarks with `build.cmd Release /benchmarks` and use [docs/profile_benchmark.md](profile_benchmark.md) for timing validation.

## Kept Decisions

| Area | Decision | Size Result |
| --- | --- | --- |
| Release profile | Keep app and benchmark Release builds on `/Os`, `/GL`, `/LTCG`, `/OPT:REF`, and non-incremental linking; keep benchmark-sensitive sources on `/O2`. | `1,783,808` to `1,465,344` bytes in the measured pass. |
| Callback type erasure | Ban production `std::function`; use `FunctionRef` for synchronous borrowed callbacks and purpose-built sink interfaces for longer lifetimes. | `1,451,008` to `1,440,768` bytes across the callback cleanup. |
| C++/CLI provider boundary | Keep STL-heavy Gigabyte provider logic in native code and keep `/clr` bridge method signatures narrow. | `1,440,768` to `1,309,184` bytes; CLR metadata dropped from `126,840` to `25,904` bytes. |
| Filesystem | Use `src/util/file_path.*` instead of `std::filesystem`. | `1,309,184` to `1,303,040` bytes. |
| Config parser/writer | Keep structured config I/O on runtime descriptor loops with non-throwing numeric parsing. | `1,303,040` to `1,253,376` bytes. |
| Native exceptions | Keep native app and benchmark targets without native C++ exception handling; keep managed bridge exception handling isolated. | `1,253,376` to `1,152,512` bytes. |
| Config writer | Keep full and minimal saves on one traversal. | `1,152,512` to `1,144,832` bytes. |
| Config runtime descriptors | Keep parser and writer on shared offset-based runtime descriptors. | `1,144,832` to `1,135,104` bytes across descriptor unification and offset descriptors. |
| Snapshot dump | Keep flat key/value dump parsing and descriptor-driven scalar field I/O. | `1,135,104` to `1,133,568` bytes. |
| Layout-edit dialog | Keep common selection finish and trace plumbing shared. | `1,133,568` to `1,132,544` bytes. |
| STL vector algorithms | Keep `_USE_STD_VECTOR_ALGORITHMS=0` for native app, test, and benchmark targets. | `1,132,544` to `1,107,456` bytes in that pass. |
| Color resolver | Resolve color fields through runtime config metadata and `FunctionRef`. | `1,208,320` to `1,203,712` bytes on the measured feature baseline. |
| Map tooling | Keep linker maps as side tooling through `CASEDASH_LINK_MAPS` and `build_maps.cmd`. | No shipped-size change; prevents ad hoc map setup churn. |
| Telemetry callback sink | Keep telemetry runtime callbacks on a non-owning `TelemetryUpdateSink`. | `1,363,456` to `1,360,896` bytes in that pass. |
| Benchmark target | Keep `CaseDashBenchmarks` opt-in through `/benchmarks`. | No app-size change; keeps routine builds and release validation focused on the shipped executable. |
| Small fixed lookups | Prefer compact scans, vectors, dense enum buckets, and small fixed tables over tiny `set`, `map`, and `unordered_*` structures. | `1,360,896` to `1,352,192` bytes in that pass. |
| Embedded resources | Generate compressed embedded config/localization payloads, losslessly recompress PNG/ICO assets, and keep tiny layout-edit grouping tables vector-based. | `1,352,192` to `1,331,712` bytes; `.rsrc$02` about `46.4 KiB` to `34.5 KiB`. |
| Cold diagnostics and menus | Keep headless diagnostics callback-free, scale menu fixed-array based, and layout-guide-sheet unique-class counting allocation-free. | `1,331,712` to `1,326,592` bytes. |

## Rejected Or Neutral Experiments

- Do not retry broad removal of the maintained `/O2` source override list; it did not reduce `build\CaseDash.exe`.
- Do not retry `/GF` as a standalone size win; it did not reduce the shipped executable in the measured pass.
- Do not keep `/Gy`, `/Gw`, `/OPT:ICF`, `/GF`, or `/Zc:inline` just because they sound size-oriented; the final retained code-shape wins did not need those extra flags.
- Do not retry replacing the config parser card-reference `std::set` with a flat string-view vector; it regressed the app size in the measured pass.
- Do not reintroduce `std::filesystem`, native app exceptions, production `std::function`, or MSVC STL vectorized algorithm dispatch without a measured app-size and performance reason.

## Notes

- Size numbers are comparable only within their local feature baseline. Feature additions between passes can raise the absolute executable size while a local size experiment still helps.
- Prefer deleting template instantiations, exception/RTTI machinery, duplicated descriptor paths, and cold-path heap containers over adding compression or decoding work to hot paths.
- The current 10% savings target is not visible as one safe map item. Larger remaining wins likely require deeper cold-subsystem compaction while keeping the renderer and telemetry benchmarks in range.
