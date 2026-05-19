# Size Optimization Notes

This document owns executable-size constraints, the map workflow, current size shape, and active experiment guidance. It is not a chronological log; keep only conclusions that still help the next size pass choose or avoid work. Keep benchmark workflow and shared performance research in [docs/profile_benchmark.md](profile_benchmark.md), and keep machine-specific timing ranges under `docs/performance/`.

## Constraints

- Optimize `build\CaseDash.exe`; benchmark binary size is not a release goal.
- Keep CaseDash as one executable with the full feature set. Do not split payloads into side files or remove supported hardware, diagnostics, layout-edit, localization, or default-config behavior.
- Preserve runtime performance. Prefer cold-path, resource, metadata, and linker-shape wins before touching renderer, widget draw, layout resolver, or telemetry hot paths.
- Keep embedded `resources/config.ini` and `resources/localization.ini` available through the executable.
- Keep normal builds free of map generation. Use `build_maps.cmd` for app maps and `build_maps.cmd /benchmarks` only when benchmark maps matter.
- Write map summaries under `build\`; do not rely on console-only analysis output.
- Treat MSVC map symbol sizes as estimates inferred from adjacent addresses.

## Current State

- Current measured `build\CaseDash.exe`: `956,928` bytes.
- Current app map summary: `build\CaseDash.map.summary.txt`.
- Current largest sections: `.text$mn` about `772.1 KiB`, `.rdata` about `69.8 KiB`, `.pdata` about `38.2 KiB`, `.rsrc$02` about `17.9 KiB`, and `.xdata` about `15.6 KiB`.
- Current largest project objects: `diagnostics.cpp.obj`, `layout_resolver.cpp.obj`, `dashboard_renderer.cpp.obj`, `dashboard_controller.cpp.obj`, `editors.cpp.obj`, `d2d_renderer.cpp.obj`, `layout_edit_controller.cpp.obj`, `pane.cpp.obj`, `layout_guide_sheet_renderer.cpp.obj`, `layout_edit_tree.cpp.obj`, `dashboard_app.cpp.obj`, `layout_edit_overlay_renderer.cpp.obj`, `layout_guide_sheet_planner.cpp.obj`, `dashboard_shell_ui.cpp.obj`, `CaseDash.rc.res`, `layout_guide_sheet_placement.cpp.obj`, `collector_fake.cpp.obj`, `render_thread.cpp.obj`, `config_parser.cpp.obj`, and `metrics.cpp.obj`.
- Last validation: `format.cmd changed`, `build.cmd`, `build_maps.cmd`, `lint.cmd includes changed`, `test.cmd`, `git diff --check`, and `build\CaseDash.exe /default-config /fake /exit /trace:build\size_optimization_validation_trace.txt /dump:build\size_optimization_validation_dump.txt /screenshot:build\size_optimization_validation_screenshot.png /layout-guide-sheet:build\size_optimization_validation_sheet.png /app-icon:build\size_optimization_validation_app_icon.png /app-icon-size:64 /save-full-config:build\size_optimization_validation_full_config.ini`.

## Workflow

- Build the app normally with `build.cmd`.
- Generate the app map and summary with `build_maps.cmd`.
- Inspect the maintained summary at `build\CaseDash.map.summary.txt`.
- Use the manual GitHub `Size Map Artifacts` workflow when the Windows runner's executable or linker map is needed for comparison with a local build; it uploads the app executable, full map, summary, and metadata.
- For ad hoc map inspection, run `python tools\analyze_link_map.py build\CaseDash.map --top 25`.
- For local-versus-remote symbol comparison, run `python tools\compare_link_maps.py build\CaseDash.map path\to\other\CaseDash.map --top 10`; deltas are reported as first map minus second map.
- When a size change can affect hot code, build benchmarks with `build.cmd Release /benchmarks`, use [docs/profile_benchmark.md](profile_benchmark.md) for benchmark workflow, and compare timing against the matching machine file under `docs/performance/`.

## Source Policy Guardrails

Hard size lessons and source-shape rules live in [docs/source_policy_guardrails.md](source_policy_guardrails.md). Keep that short summary in sync when source-policy checks are added, removed, or materially changed, including review-enforced rules that are too noisy to lint.

## Retained Size Shape

### Build And Link Profile

- Release app and benchmark builds use `/Os`, `/GL`, `/LTCG`, `/OPT:REF`, and non-incremental linking. Benchmark-sensitive renderer, widget, layout, telemetry, and benchmark-harness sources keep `/O2` so size work does not distort maintained performance loops.
- Normal Release sources use scoped `/Ob1`; only measured cold or orchestration sources keep `/Ob0`. The current noinline split keeps startup, diagnostics, display, dashboard app/controller orchestration, crash report, app icon export, and fake-telemetry generation on the cold path while allowing config parser/writer, dashboard shell UI, FPS service, snapshot dump, monitor enumeration, modeless layout-edit dialog helpers, and theme preview to use the normal Release path.
- The shipped Release app keeps `/DYNAMICBASE:NO`, `/GS-`, `/Zc:threadSafeInit-`, and `/MERGE:.nep=.text` as explicit size tradeoffs. Record any future security-relevant flag change next to its measured executable result.
- `CaseDashBenchmarks` stays opt-in through `/benchmarks`; routine release validation focuses on the shipped executable.

### Resources And Text

- The embedded default config, localization catalog, and generated resource-string catalog share one CDLZ text atlas. The atlas keeps config, localization, and first-source-use resource strings in deterministic order, with extended 12-bit-offset LZSS token parsing.
- `RES_STR("...")` literals compile to collision-checked FNV-1a ids, not generated literal tables. Runtime lookup uses the loaded text atlas plus an open-addressed hash table so trace and diagnostics paths keep O(1) catalog access after their prefix gate passes.
- User-visible UI copy belongs in `resources/localization.ini`. Direct `RES_STR` text is reserved for trace formats, compact localization keys, telemetry diagnostics payloads, and diagnostics-only errors.
- The fallback `resources\app.ico` keeps only `16`, `32`, and `64` frames. Runtime themed icons provide live and generated arbitrary-size icon assets.
- Panel icons stay in one fixed 64 x 64 vertical 8-bit grayscale PNG mask atlas and render through target-local alpha masks. Keep PNG and ICO resource optimization lossless, and strip nonessential PNG ancillary chunks only when visual validation remains healthy.
- The shipped executable omits duplicate Win32 `VERSIONINFO` string resources; full user-visible version, build, and commit text lives in generated C++ constants for the `About CaseDash` dialog.

### Metadata And Descriptors

- Config parsing, config writing, color resolution, layout-edit parameter edits, and snapshot dump scalar I/O use shared reflected descriptors rather than duplicated field paths.
- Runtime config descriptor arrays are constexpr where possible. Fixed descriptor keys use literal pointer plus byte length when the source arrays own the lifetime.
- Layout-edit parameter metadata stays in one ordered metadata array. Do not reintroduce per-parameter function-local statics, unused info-table field pointers, or a parameter-only mirror table.
- Byte-backed enum storage is useful only in measured fixed domains under 256 entries, such as `ValueFormat` and selected layout-edit anchor, guide, identity, field, and selection-highlight enums.

### Small Data Shapes

- Tiny fixed domains use compact scans, vectors, dense enum buckets, or fixed-slot caches instead of broad STL containers. This applies to metric render caches, widget layout metadata, layout resolver parsed-widget info, layout-edit extent caches, renderer brush/text caches, context-menu command ranges, and exact metric binding lookups.
- Hash lookup stays where input cardinality proves it. FPS GPU Engine raw counters use the package-owned open-addressed counter map because process-heavy machines expose hundreds of per-engine instances.
- Metric-list and drive-usage widgets consume borrowed fixed-slot row lookups where the data shape is fixed. Keep repeated drive-usage header labels and edit anchors on descriptor tables instead of duplicated draw and active-region blocks.
- Retained-history fixed CPU/GPU/network/storage series stay indexed by encoded vector slots; dynamic board temp and fan histories remain vector-scanned.

### Boundaries And Formatting

- Keep runtime text and source literals UTF-8 until a Win32, registry, filesystem, or managed bridge boundary needs UTF-16. Convert the command line to a UTF-8 `std::vector<std::string>` once in `main`, and keep command-line switch lookup narrow.
- Use `Trace::WriteFmt`, `Trace::WriteLazyFmt`, and resource-string-aware `FormatText`, `AssignFormat`, and `AppendFormat` for shared trace, diagnostics, dump, color, crash-report, metric, and cold UI text.
- Keep dump escaping local to snapshot dump because dumps are parsed later. Trace quote marks stay in format strings instead of a general trace quote/escape helper.
- Keep HRESULT formatting on `AppendHresult` and narrow error helpers instead of building temporary strings with repeated concatenation.

### Cold UI, Diagnostics, And Layout-Edit

- The modeless layout-edit dialog keeps a compact resource shell and creates child controls at runtime from seed tables. Control labels stay UTF-8 until the `CreateWindowExW` boundary.
- Layout-edit previews mutate the live config through controller methods when the edit is no-fail and local, instead of copying full `AppConfig` snapshots.
- Large cold payloads cross layout-edit and diagnostics boundaries as borrowed pointers or bool/out-parameters when that avoids copying optional records. Small optional values stay idiomatic `std::optional`.
- Layout-edit dirty tracking marks mutations as possibly dirty during the edit loop and performs exact saved-layout comparison only at prompt boundaries through config-difference metadata and selected-layout structure comparison.
- Layout guide sheet planning, placement, trace, and rendering share compact callout records, bitmask coverage scoring, and trace-only sidecars that are populated only when a trace sink is supplied.

### Runtime And Providers

- Telemetry runtime, FPS ETW processing, render-thread handoff, trace locking, and app telemetry handoff use direct Win32 thread/event handles plus `LightweightMutex` instead of STL threading wrappers.
- Provider boundaries keep broad STL or managed metadata out of the app where practical: Gigabyte SIV logic stays native with narrow `/clr` bridge signatures, Intel Level Zero and NVML dynamic entry-point loads stay concrete at call sites, and process/provider names convert at the OS boundary.
- Network selection streams visible candidates directly into retained telemetry state, with the selected candidate tracked in place.
- Synthetic telemetry history generation keeps spec records static and moves generated history vectors into retained-history entries.

## Active Negative Guidance

### Toolchain And Linker Trials

- Do not keep `/Gy`, `/Gw`, `/GF`, `/OPT:ICF`, `/Zc:inline`, `/O1`, `/Zc:throwingNew-`, `/CETCOMPAT:NO`, `/FIXED`, `/HIGHENTROPYVA:NO`, Release `/Zi` removal, or broad `/O2` override removal as standalone size levers without a fresh map showing a retained executable win.
- Do not broaden `/Ob0` into benchmark-sensitive renderer, layout, telemetry, widget, tooltip, trace-formatting, resource-loading, localization, config-resolution, metric-catalog, or layout-edit metadata sources. Extra call bodies and unwind metadata have outweighed deleted inline expansion in those areas.
- Do not merge `.rsrc` or `.pdata` as Release size levers. `.rsrc` merging fails under the current linker, and `.pdata` merging did not reduce the shipped executable.
- Keep the application manifest readable; manifest minification reduced the intermediate resource object but not the shipped executable.

### Resource And Catalog Trials

- Do not byte-pack `ResourceStringId`; the catalog needs room for growth.
- Do not add a generated resource-string offset table, store the resource-string catalog with NUL separators, or remove the loaded lookup. The extra storage or runtime scans outweigh the one-time load simplification.
- Do not change the CDLZ token split, add BWT/MTF/run/Huffman pre-passes, sort the resource-string catalog, reorder atlas sections, or move layout-node descriptor hints into `RES_STR` as standalone changes. The retained first-source-use atlas and extended 12/4 stream are the current measured shape.
- Do not put direct English UI messages into `RES_STR`. Localization owns user-visible copy; `RES_STR` may carry compact localization keys and diagnostics payload text.
- Keep newline-bearing user-visible message formats split around the newline unless the resource-string catalog format changes.

### Data-Structure Trials

- Do not broadly replace every `std::unordered_map` with vectors. Tiny caches benefit from scans and fixed slots, but large or process-heavy domains still need hash lookup.
- Do not flatten FPS ETW GPU raw counter maps or replace the remaining concrete string-key pointer/index maps with a shared wrapper unless the map summary shows deleted code beyond the wrapper cost.
- Do not replace small `std::optional` values with manual bool-plus-storage. Revisit optional-shaped APIs only when the payload is large enough that the map shows a real win.
- Do not replace config runtime fixed-field parsing with a local comma scanner, snapshot dump scalar helpers with string-view/pointer write boundaries, or diagnostics integer parsing with manual local parsers as isolated changes.
- Keep byte-backed enum work focused on measured domains. Broad enum macro packing or isolated enum packing has regressed or stayed flat.

### Cleanup-Shaped Trials

- Treat helper extraction, local string rewrites, direct loops, and "simpler" container consolidation as hypotheses, not free wins. Many isolated cleanups shrink a local symbol but grow the executable by adding helper bodies, metadata, or section slack.
- Do not retry isolated helper extraction for context menus, dashboard shell message boxes, module paths, derived-color rows, right-pane single-control rows, or layout-guide-sheet scan/placement helpers unless a broader pass deletes surrounding machinery.
- Do not retry isolated append-builder rewrites for diagnostics errors, crash-report values, trace bool text, or tiny literal suffix joins while the shared formatting helper remains the retained shape.
- Do not retry isolated layout-edit or guide-sheet shape experiments that only move code across objects: score-only guide-sheet leader helpers, broad placement helper extraction, tooltip target aliasing, guide-sheet callout target struct compaction, direct variant emplacement, or current tooltip target initializer-list rewrites.

### Current Split And Boundary Trials

- Keep `dashboard_shell_ui.cpp` on the maintained speed-source list and off the cold `/Ob0` list unless a broader menu payload pass proves a win.
- Keep layout guide sheet renderer sources on the current optimization split unless benchmark data says the direct placement and rendering paths no longer need it.
- Keep `diagnostics.cpp`, `app_icon_export.cpp`, `crash_report.cpp`, `display_config.cpp`, `dashboard_app.cpp`, `dashboard_controller.cpp`, and `collector_fake.cpp` on the cold `/Ob0` list. Removing `/Ob0` from those sources regresses the current noinline shape.
- Keep dynamic WTS loading out of the current session-notification path; both retained-module and load/call/free variants regressed the current baseline.

## Working Notes

- Size numbers are comparable only within their local feature baseline. Feature additions between passes can raise the absolute executable size while a local size experiment still helps.
- Prefer deleting template instantiations, exception/RTTI machinery, duplicated descriptor paths, and cold-path heap containers over adding compression or decoding work to hot paths.
- Leave concise `Size:` comments next to measured non-obvious source choices, especially where a normal cleanup would reintroduce larger STL containers, templates, or full-config copies.
- Measure the final executable after each retained change; object or section reductions often disappear into file-alignment slack.
- Further large wins are unlikely to come from one remaining resource row or helper extraction. Larger remaining savings likely require broad architectural improvements that identify reusable chunks of functionality across cold and hot paths while keeping renderer and telemetry benchmarks in range. Treat duplicated or inefficient hot-path code as a valid size target when a smaller shared shape also preserves or improves runtime speed, or when an explicit toolchain tradeoff records the security impact.
