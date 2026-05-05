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

- Current measured `build\CaseDash.exe`: `1,170,432` bytes.
- Current app map summary: `build\CaseDash.map.summary.txt`.
- Current largest sections: `.text$mn` about `949.9 KiB`, `.rdata` about `88.1 KiB`, `.rsrc$02` about `32.5 KiB`, `.pdata` about `20.8 KiB`, `.xdata` about `19.8 KiB`.
- Current largest project objects: `diagnostics.cpp.obj`, `editors.cpp.obj`, `dashboard_app.cpp.obj`, `dashboard_shell_ui.cpp.obj`, `dashboard_renderer.cpp.obj`, `layout_resolver.cpp.obj`, `CaseDash.rc.res`, `layout_guide_sheet_renderer.cpp.obj`, `layout_edit_controller.cpp.obj`, and `dashboard_controller.cpp.obj`.
- Last validation: `format.cmd fix changed`, `build.cmd`, `test.cmd`, `python tools\source_dependency_graph.py --skip-svg --check`, `build\CaseDash.exe /default-config /fake /exit /trace:build\validation_size_trace.txt /dump:build\validation_size_dump.txt`, `build_maps.cmd`, and `format.cmd changed`.

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
| Metric render caches | Keep per-frame `MetricSource` caches and exact metric binding lookup vector-based instead of `std::unordered_map`; these caches have tiny key sets and are hit during rendering. | `1,326,592` to `1,318,400` bytes. A plain-struct cache-entry variant regressed to `1,318,912` bytes, so keep the `std::pair` vector shape for now. |
| Trace formatting | Keep trace escaping, quoting, and point formatting in `Trace` instead of private copies in diagnostics, dashboard, and layout-edit dialog code. | `1,318,400` to `1,317,376` bytes across the shared trace formatter passes. |
| Board provider shared helpers | Keep common MSI Center and Gigabyte SIV board sensor mapping, requested-index insertion, metric reset, reading-name extraction, and reading-to-metric application in `system_info_support`. | `1,317,376` to `1,315,328` bytes. |
| Runtime field offsets | Compute reflected field offsets from type facts instead of constructing owner objects. | `1,315,328` to `1,294,336` bytes. |
| Small sorts | Keep direct production `std::stable_sort`, `std::sort`, and `std::unique` out of tiny cold-list paths; use concrete helper sorts or small insertion sorts. | `1,294,336` to `1,267,712` bytes. |
| Layout node paths | Reuse layout node path vectors with push/pop traversal instead of creating path copies for each child. | `1,267,712` to `1,265,664` bytes. |
| Layout-edit tree builders | Keep cold tree builder outputs as bool plus out-parameter instead of returning large `std::optional<LayoutEditTreeNode>` values. | `1,267,712` to `1,265,664` bytes in the measured pass. |
| Small edit caches | Keep layout resolver parsed-widget info, layout-edit extent cache, and board requested-index lookup as flat vectors for tiny domains. | `1,265,664` to `1,263,104` bytes across the measured cache passes. |
| Concrete storage helpers | Keep storage-drive sorting and string-list assignment on concrete helper functions instead of local STL algorithm instantiations. | `1,263,104` to `1,257,984` bytes. |
| Pane layout helpers | Keep repeated dialog control visibility, invalidation, and max calculations in non-template helper functions. | `1,257,984` to `1,255,424` bytes. |
| Initializer-list cleanup | Avoid cold vector/string initializer-list machinery in guide-sheet planning, fake telemetry literals, dialog combo population, and max/min calculations. | `1,255,424` to `1,253,888` bytes across measured passes. |
| Renderer similarity scans | For tiny result sets, scan existing vectors instead of adding extra seen-key vectors and staged similarity containers. | `1,253,888` to `1,248,256` bytes across hit-test, dashboard-renderer, and overlay passes. |
| Layout-edit weight previews | Update adjacent guide weights directly instead of rebuilding the whole child-weight vector for one separator edit. | `1,248,256` to `1,246,720` bytes. |
| Layout-edit direct previews | Apply font and pure layout preview edits through controller methods that mutate the live config instead of copying a full `AppConfig` in the dialog host. | `1,246,720` to `1,245,696` bytes. |
| Config mutation tail | Keep the repeated layout-edit config refresh tail in one noinline controller helper. | `1,245,696` to `1,245,184` bytes. |
| Saved layout snapshot | Allocate the layout-edit saved-layout snapshot only while edit mode is active instead of default-constructing a second `LayoutConfig` in session state. | `1,245,184` to `1,241,088` bytes. |
| Wide string boundaries | Keep shared `util/strings` UTF-8-only beyond conversion helpers; convert the Gigabyte SIV registry display name to UTF-8 at the registry boundary and keep font-family wide sorting local to the Win32 dialog boundary. | Executable-neutral at `1,241,088` bytes. |
| Process path capture | Reuse `util/paths` fixed-buffer executable and working-directory helpers for elevated relaunch instead of keeping a second vector-based path reader in `main.cpp`. | `1,241,088` to `1,239,040` bytes. |
| Command-line switch scans | Keep command-line switch lookup on a narrow argv scanner and a purpose-built elevated-relaunch helper instead of materializing `std::vector<std::wstring>` for every scan. | `1,239,040` to `1,237,504` bytes. |
| Layout-edit editor visibility | Select the active layout-edit selection editor by enum in one helper instead of repeating long boolean visibility packs at each populate branch. | `1,237,504` to `1,236,480` bytes. |
| Wide string boundary pass | Keep layout-edit dialog combo/text setters, fixed-buffer checks, color numeric formatting, font sample text, diagnostics output errors, and file-path traces on UTF-8 or stack-buffer helpers until the Win32 boundary. | `1,236,992` to `1,232,384` bytes in that measured pass. |
| Layout-edit parameter metadata | Keep layout-edit parameter find/apply behavior on root-offset metadata and shared accessors instead of per-field template callbacks. | `1,232,384` to `1,214,976` bytes; per-field `ApplyColorFieldEdit`, `ApplyFontFieldEdit`, and `DeferredRootFieldLens::Set` symbols are removed. |
| Layout-guide-sheet overlay state | Reuse one layout-guide-sheet draw overlay state instead of copying the full `DashboardOverlayState` for every card and callout overlay draw. | `1,214,976` to `1,214,464` bytes. |
| Runtime config selection updates | Apply resolved telemetry selections in place and mutate no-fail controller display/layout edits directly instead of round-tripping full `AppConfig` copies. | `1,214,464` to `1,212,928` bytes. |
| Fake telemetry synthetic histories | Move generated synthetic history vectors into retained-history entries and accept literal series/drive labels without temporary `std::string` parameters. | `1,212,928` to `1,211,392` bytes; `BuildSyntheticTelemetryDump` dropped from about `5.8 KiB` to `4.1 KiB`. |
| Command-line and trace literal boundaries | Keep command-line switch scans and diagnostics/layout-edit trace event names on literal pointer APIs when the values are fixed command-line switches, event names, drag kinds, or close reasons. | `1,211,392` to `1,200,128` bytes across the measured switch and trace-boundary passes. |
| Runtime config descriptor storage | Keep runtime field descriptor keys as literal pointer plus byte length, and keep descriptor arrays in template static storage instead of function-local statics. | `1,200,128` to `1,195,520` bytes; `config_runtime_fields.cpp.obj` dropped from about `15.7 KiB` to `11.2 KiB`. |
| Layout-edit active editor state | Keep the current layout-edit selection editor kind and metric-binding visibility in dialog state, and pass a single editor kind into the pane visibility helper. | `1,195,520` to `1,195,008` bytes. |
| Network adapter selection | Keep adapter selection data, UTF-8 names, match rank, and traffic counters in one candidate record, and use one automatic-candidate comparison helper instead of repeating wide/UTF-8 match chains. | `1,195,008` to `1,193,984` bytes. |
| FPS ETW provider small caches | Keep FPS ETW source names as an enum conversion, keep process names, GPU usage, and present-event buckets as flat vectors for tiny active process sets, and keep GPU raw counter lookup hash-based because GPU Engine exposes hundreds of per-engine instances on process-heavy machines. | `1,193,984` to `1,186,816` bytes across the measured FPS provider passes. |
| Retained history indexes | Keep retained-history series in the dump-facing vector, cache fixed CPU/GPU/network/storage histories by `RetainedHistoryKey` encoded vector indices, and leave dynamic board temp/fan histories on a vector scan. | `1,186,816` to `1,182,208` bytes; a plain vector scan reached the same size, so keep the indexed shape for predictable fixed-key lookups. |
| Widget layout metadata | Cache parsed widget facts as `WidgetClass` plus layout flags instead of storing cloneable widget prototypes, keep widget class, hover, and fixed-height facts enum-based in `WidgetLayout`, and expose gauge-only group finalization through the public widget package boundary. | `1,182,208` to `1,177,600` bytes; removing the `Class()` and fixed-height virtuals were neutral in isolation but kept as source simplification once `WidgetLayout` owned class identity. |
| Worker thread handles | Keep the telemetry runtime, FPS ETW processor, and FPS service worker on direct Win32 thread and event handles instead of `std::thread` and `std::condition_variable` wrappers. | `1,177,600` to `1,177,088` bytes; `.text$mn` dropped from about `954.7 KiB` to `954.1 KiB`. |
| PNG and ICO resource metadata | Strip nonessential PNG ancillary chunks from app icon and section icon resources while preserving image pixel data. | `1,177,088` to `1,176,576` bytes; `.rsrc$02` dropped from about `34.5 KiB` to `34.0 KiB`. |
| Panel icon atlas | Keep panel icons in one fixed 64 x 64 vertical 8-bit grayscale PNG mask atlas and draw cropped atlas slots through a target-local alpha mask instead of caching per-icon sources and scaled Direct2D bitmaps. | `1,176,576` to `1,171,456` bytes; `.text$mn` dropped from about `954.2 KiB` to `949.6 KiB` and `.rsrc$02` dropped from about `34.0 KiB` to `33.5 KiB`. The follow-up alpha-only PNG conversion shrank `resources\panel_icons.png` from `2,285` to `1,572` bytes but did not cross another executable file-alignment boundary. |
| Text resource atlas | Compress the embedded default config and localization catalog as one LZSS text atlas with the config length followed by both files instead of two separate compressed RCDATA resources. | `1,171,456` to `1,170,432` bytes; `.rsrc$02` dropped from about `33.5 KiB` to `32.5 KiB` while `.text$mn` grew to about `949.9 KiB` for atlas slicing. The generated compressed text payload dropped from `6,066` to `5,790` bytes. |

## Rejected Or Neutral Experiments

- Do not retry broad removal of the maintained `/O2` source override list; it did not reduce `build\CaseDash.exe`.
- Do not retry `/GF` as a standalone size win; it did not reduce the shipped executable in the measured pass.
- Do not keep `/Gy`, `/Gw`, `/OPT:ICF`, `/GF`, or `/Zc:inline` just because they sound size-oriented; the final retained code-shape wins did not need those extra flags.
- Do not retry replacing the config parser card-reference `std::set` with a flat string-view vector; it regressed the app size in the measured pass.
- Do not broadly replace every `std::unordered_map` cache with vectors. The dashboard renderer metric-definition cache regressed the app from `1,318,400` to `1,319,936` bytes in the measured pass.
- Do not retry simple `ShowContextMenu` submenu-helper extraction. It shrank the individual `ShowContextMenu` symbol but grew `build\CaseDash.exe` from `1,318,400` to `1,319,424` bytes because it moved code into helpers without deleting shared machinery.
- Do not retry a shared MSI/Gigabyte uninstall-registry scanner with a function-pointer display-name matcher. It grew the app from `1,315,328` to `1,316,352` bytes because the shared helper plus matcher calls outweighed the deleted local loops.
- Direct theme/layout combo population removed local vector code but was executable-neutral; keep only as local simplification, not as a primary size lever.
- A shared board metric binding parser removed duplicate source and trimmed `dashboard_shell_ui.cpp.obj`, but was executable-neutral.
- Explicit menu command lookup helpers for `ShowContextMenu` were executable-neutral and slightly larger by object; do not retry that shape.
- Compact telemetry-setting extraction in `ApplyConfigSnapshot` regressed by 512 bytes; keep the full previous `AppConfig` snapshot there.
- Direct controller-side metric preview regressed by 512 bytes; keep metric preview on the existing config snapshot path.
- UTF-8-only tooltip assembly shrank `BuildLayoutEditTooltipTextForPayload` from about 6.3 KiB to 5.9 KiB and reduces per-tooltip conversions, but the final executable stayed flat in that isolated trial.
- Noinline `LayoutConfig::operator==` moved the full-layout equality chain out of `RefreshLayoutEditSessionDirtyFlag`, but was executable-neutral in the measured pass; keep only as a guard against future duplicate callers.
- Copy-then-move restore of the saved layout edit snapshot regressed by 5,120 bytes; keep direct copy assignment for restore.
- Flat board sensor name bindings removed unordered-map use from the board config/settings surface but grew the app by 2,048 bytes because the helper/vector code outweighed deleted hash machinery. Keep the existing maps there for now.
- Manual loops for command-line wide-string trim and path normalization regressed by 512 bytes versus the existing STL algorithm shape; keep the measured algorithm code there.
- A shared module-path helper for executable and crash-report module paths regressed by 512 bytes; keep the duplicated local shapes.
- In-place layout guide snap-weight updates shrank the local drag symbol from about 5.8 KiB to 5.7 KiB and benchmarked in range, but the shipped executable stayed flat at `1,236,992` bytes. Keep it only as copy-avoidance cleanup, not as a size lever.
- Release hardening metadata is not a practical size lever. `/guard:cf- /guard:ehcont- /volatileMetadata-` plus `/GUARD:NO /EMITVOLATILEMETADATA:NO` did not reduce the executable; keep the normal compiler hardening profile unless a future size target explicitly accepts that security tradeoff.
- Do not chase small `std::optional` rewrites. Keep `std::optional` for small values, pointers, and clear modern C++ intent; only revisit optional-shaped APIs when the payload or caller structure is large and a map proves a real win.
- Dashboard shell UTF-8 menu label helpers regressed the executable from `1,235,456` to `1,236,480` bytes in the measured trial. Keep the existing wide menu label construction unless a broader menu architecture change deletes more code.
- Dashboard controller and layout-guide-sheet path reuse were executable-neutral in isolation. Keep local reuse when it clarifies cold file-output paths, but do not treat it as a primary size lever.
- Telemetry update by-value/rvalue handoff regressed from `1,214,976` to `1,217,536` bytes. Keep the existing const-reference sink handoff unless a future runtime-copy reduction also proves a shipped-size win.
- Passing layout-edit config snapshots by value and moving through the dashboard shell removed one local copy-assignment symbol but grew the executable from `1,212,928` to `1,222,656` bytes. Keep the const-reference preview snapshot path.
- A broad `Trace::Write(std::string_view)`/chunked-write path regressed the executable to `1,238,016` bytes. Keep the existing `Trace::Write(const char*)` and `Trace::Write(const std::string&)` surface and use narrower literal overloads at higher-level call boundaries instead.
- Changing `Trace::BoolText` to return literal pointers regressed from `1,200,128` to `1,200,640` bytes after the required call-site reshaping. Keep the current `std::string` return unless a broader trace builder removes more concatenation code.
- Pointer-only runtime field descriptor keys reached the same shipped size as pointer-plus-length keys, but grew parser and writer objects. Keep the byte length in descriptor padding so config consumers can compare `std::string_view` values without the old 24-byte descriptor shape.
- A shared derived-color slider row helper was executable-neutral at `1,193,984` bytes. Keep the explicit row layout unless a broader pane restructuring deletes more surrounding code.
- Direct construction of the diagnostics reload effective config was executable-neutral at `1,193,984` bytes. Keep the existing full-config copy because the clearer code shape is not a current size lever.
- Do not flatten the FPS ETW provider's GPU raw counter maps. The maintained process-heavy validation machine reported `632` GPU Engine counter instances and `28` GPU Engine process ids, so raw counter matching needs hash lookup even though the active process-name, GPU-usage, and present-event sets stay tiny.
- A shared `StringPointerMap`/`StringIndexMap` wrapper around `std::unordered_map<std::string, const void*>` did not pay off for the remaining string-key pointer/index maps. The leaner raw-pointer pimpl version grew `build\CaseDash.exe` to `1,187,328` bytes, so keep concrete cache shapes instead of centralizing hash tables behind that wrapper.
- Do not replace the layout-edit metric-list order row mutation lambdas with a discriminator-driven shared helper. That trial grew `build\CaseDash.exe` from `1,177,600` to `1,178,112` bytes.
- Do not replace the layout-guide-sheet renderer's local `std::find_if` scans with concrete scan helpers. That trial grew `build\CaseDash.exe` from `1,177,600` to `1,178,112` bytes.
- A narrow removal of the layout-guide-sheet sources from the maintained `/O2` override list was executable-neutral at `1,177,600` bytes; keep the current optimization split unless benchmark data says those files no longer need it.
- Replacing `TooltipPayload` `std::visit` dispatch with explicit `std::get_if` chains removed the local visit symbols but was executable-neutral at `1,177,088` bytes in isolation.
- Replacing the dashboard-shell tooltip-target `std::get` sites with `std::get_if` also stayed executable-neutral once measured with the tooltip-payload change; `std::bad_variant_access` support moved to the next variant caller instead of disappearing from the app.
- Do not replace the diagnostics screenshot hover path with local hit-test helpers to avoid constructing `LayoutEditController`. That trial inflated `diagnostics.cpp.obj` and was not a shipped-size win.
- Do not reintroduce `std::filesystem`, native app exceptions, production `std::function`, or MSVC STL vectorized algorithm dispatch without a measured app-size and performance reason. `lint.cmd` blocks maintained source and test files from using `std::filesystem` or including `<filesystem>`.

## Notes

- Size numbers are comparable only within their local feature baseline. Feature additions between passes can raise the absolute executable size while a local size experiment still helps.
- Prefer deleting template instantiations, exception/RTTI machinery, duplicated descriptor paths, and cold-path heap containers over adding compression or decoding work to hot paths.
- Prefer vectors and compact scans for tiny fixed domains. For string-to-value maps that are genuinely performance-sensitive and not tiny, consider a narrow non-template project-owned hash helper under `src/util` instead of repeating broad STL hash-table instantiations.
- Treat icon PNG metadata as nonessential unless a future visual validation shows a platform-specific color-management regression; keep the pixel payload and image sizes intact.
- Keep the panel-icon atlas simple and fixed-slot. A literal per-draw tinted bitmap reached `1,170,944` bytes but regressed `edit-layout` paint to about `2.50 ms`; a direct WIC-to-Direct2D A8 upload also reached `1,170,944` bytes but failed screenshot rendering. Keep the target-local alpha-mask version unless a future icon-heavy benchmark proves another shape is better.
- Leave concise `Size:` comments next to measured non-obvious source choices, especially where a normal cleanup would reintroduce larger STL containers, templates, or full-config copies.
- UTF-8 boundary discipline is plausible mostly in UI, diagnostics, command-line, and vendor-boundary code. The current systematic audit concentrated on the highest-density wide-string files: layout-edit dialog `pane`, `util`, and `editors`, diagnostics output handling, and dashboard/menu boundaries. Do not push extra UTF-8 to UTF-16 conversions into renderer or telemetry hot loops without benchmark evidence.
- `std::optional` is not a standalone size target. Preserve it for small values and pointer-like lookups; the earlier tree-builder win came from avoiding large cold payload copies, not from removing optional itself.
- The current 10% savings target is not visible as one safe map item. Larger remaining wins likely require deeper cold-subsystem compaction while keeping the renderer and telemetry benchmarks in range.
