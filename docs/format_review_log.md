# Format Review Log

Review date: 2026-05-30.

Scope: reviewed the formatter-produced `git diff` against `docs/format.md` and `.cpp-format`. The formatter diff contains 225 files and `10,564 insertions(+), 5,826 deletions(-)`.

Accepted formatter-owned changes include whitespace, line breaks, indentation, compact-or-split list layout, declaration grouping, control-brace normalization, macro continuation spacing, trailing-comma removal outside enum bodies, and documented parenthesized-initializer ambiguity behavior.

`git diff --check` reported `new blank line at EOF` in `src/config/metric_display_style.h` and `src/config/widget_class.h`. Both files end with `#undef`; empty lines are separators only, so a final `#undef` must not leave an empty line at EOF.

## Suspect Findings

### Token Rewrites

`docs/format.md` preserves token text except for include sorting, trailing-comma normalization, control-brace normalization, and safe adjacent ordinary string-literal concatenation. Win32 `TRUE` and `FALSE` to C++ `true` and `false` is outside that list. Representative examples: `if (copied == FALSE)` became `if (copied == false)` in `src/dashboard/dashboard_app.cpp`, and `CreateEventA(nullptr, TRUE, FALSE, nullptr)` became `CreateEventA(nullptr, true, false, nullptr)` in `tests/telemetry_runtime_tests.cpp`.

| File | Count evidence |
| --- | --- |
| `src/dashboard/dashboard_app.cpp` | TRUE 11->0, FALSE 40->0 |
| `src/dashboard/dashboard_shell_ui.cpp` | TRUE 13->0, FALSE 8->0 |
| `src/dashboard/dashboard_titlebar.cpp` | FALSE 1->0 |
| `src/dashboard/dashboard_tooltip.cpp` | TRUE 2->0, FALSE 1->0 |
| `src/dashboard/dashboard_window_chrome.cpp` | TRUE 1->0, FALSE 2->0 |
| `src/dashboard/display_placement_menu_bitmap.cpp` | FALSE 1->0 |
| `src/dashboard/fps_service.cpp` | TRUE 2->1, FALSE 4->1 |
| `src/dashboard_renderer/impl/render_thread.cpp` | TRUE 3->0, FALSE 3->0 |
| `src/diagnostics/crash_report.cpp` | FALSE 2->0 |
| `src/display/monitor.cpp` | TRUE 3->0, FALSE 2->0 |
| `src/layout_edit_dialog/impl/dialog_proc.cpp` | TRUE 35->0, FALSE 4->0 |
| `src/layout_edit_dialog/impl/editors.cpp` | TRUE 15->0, FALSE 10->0 |
| `src/layout_edit_dialog/impl/pane.cpp` | TRUE 8->0, FALSE 6->0 |
| `src/layout_edit_dialog/impl/util.cpp` | TRUE 16->0, FALSE 1->0 |
| `src/layout_edit_dialog/layout_edit_dialog.cpp` | TRUE 1->0, FALSE 7->0 |
| `src/renderer/impl/d2d_renderer.cpp` | TRUE 1->0, FALSE 1->0 |
| `src/telemetry/board/asus/board_asus_armoury_crate.cpp` | FALSE 2->0 |
| `src/telemetry/board/lenovo/board_lenovo_vantage.cpp` | FALSE 1->0 |
| `src/telemetry/fps/fps_etw_provider.cpp` | FALSE 1->0 |
| `src/telemetry/impl/collector_cpu.cpp` | FALSE 1->0 |
| `src/telemetry/impl/collector_network.cpp` | FALSE 2->0 |
| `src/telemetry/impl/collector_storage.cpp` | FALSE 1->0 |
| `src/telemetry/telemetry.cpp` | FALSE 2->0 |
| `tests/benchmarks.cpp` | TRUE 2->0, FALSE 2->0 |
| `tests/telemetry_runtime_tests.cpp` | TRUE 1->0, FALSE 1->0 |

### Macro Decltype Loss

The formatter emitted `reinterpret_cast<decltype>(...)`, which violates token preservation and appears syntax-breaking.

| File | Example |
| --- | --- |
| `src/telemetry/gpu/intel/gpu_intel_level_zero.cpp` | `decltype(function)` became `decltype` in `CASEDASH_LOAD_OPTIONAL`. |
| `src/telemetry/gpu/nvidia/gpu_nvidia_nvml.cpp` | `decltype(function)` became `decltype` in loader macros. |

### Ternary Spacing

`docs/format.md` requires spaces around ternary operators; these added lines lost the space before `:`.

| File | Example |
| --- | --- |
| `src/layout_edit_dialog/impl/tree.cpp` | line 247: `TreeViewportSnapshot{}: CaptureTreeViewportSnapshot(...)` |
| `src/layout_guide_sheet/impl/layout_guide_sheet_renderer.cpp` | lines 112-113: `std::string_view{}: std::string_view{...}` |
| `src/telemetry/gpu/intel/gpu_intel_level_zero.cpp` | line 307: `std::string{}: value` |
| `src/util/trace_timing.cpp` | line 120: `std::chrono::nanoseconds{}: HighPrecisionTimer::Elapsed(...)` |

### Initializer Padding

`docs/format.md` says there is no padding inside one-line initializer braces.

| File | Example |
| --- | --- |
| `src/dashboard_renderer/impl/layout_edit_overlay_renderer.cpp` | lines 1257-1258: `LayoutContainerEditKey{ owner... }` |
| `src/dashboard_renderer/impl/layout_resolver.cpp` | lines 514-515: `LayoutContainerEditKey{ owner... }` |
| `tests/layout_edit_hit_test_tests.cpp` | lines 217-220: `RenderPoint{ x, y }` and variants |

### EOF Separator Lines

Empty lines are separators only, so formatter-inserted `#undef` separators must not survive at EOF.

| File | Example |
| --- | --- |
| `src/config/metric_display_style.h` | final `#undef CASEDASH_METRIC_DISPLAY_STYLE_ITEMS` was followed by an empty line at EOF. |
| `src/config/widget_class.h` | final `#undef CASEDASH_WIDGET_CLASS_ITEMS` was followed by an empty line at EOF. |

## Per-File Review

| File | Review |
| --- | --- |
| `src/config/color_expression.cpp` | OK. |
| `src/config/color_math.cpp` | OK. |
| `src/config/color_resolver.cpp` | OK. |
| `src/config/color_resolver.h` | OK. |
| `src/config/config.h` | OK. |
| `src/config/config_file_io.cpp` | OK. |
| `src/config/config_io.cpp` | OK. |
| `src/config/config_io.h` | OK. |
| `src/config/config_parser.cpp` | OK. |
| `src/config/config_parser.h` | OK. |
| `src/config/config_resolution.cpp` | OK. |
| `src/config/config_runtime_fields.cpp` | OK. |
| `src/config/config_runtime_fields.h` | OK. |
| `src/config/config_telemetry.cpp` | OK. |
| `src/config/config_writer.cpp` | OK. |
| `src/config/config_writer.h` | OK. |
| `src/config/metric_board_binding.cpp` | OK. |
| `src/config/metric_board_binding.h` | OK. |
| `src/config/metric_display_style.h` | Suspect - EOF separator line after final `#undef`. |
| `src/config/widget_class.h` | Suspect - EOF separator line after final `#undef`. |
| `src/dashboard/autostart.cpp` | OK. |
| `src/dashboard/dashboard_app.cpp` | Suspect - token rewrite (TRUE 11->0, FALSE 40->0). |
| `src/dashboard/dashboard_app.h` | OK. |
| `src/dashboard/dashboard_controller.cpp` | OK. |
| `src/dashboard/dashboard_controller.h` | OK. |
| `src/dashboard/dashboard_shell_ui.cpp` | Suspect - token rewrite (TRUE 13->0, FALSE 8->0). |
| `src/dashboard/dashboard_shell_ui.h` | OK. |
| `src/dashboard/dashboard_titlebar.cpp` | Suspect - token rewrite (FALSE 1->0). |
| `src/dashboard/dashboard_titlebar.h` | OK. |
| `src/dashboard/dashboard_tooltip.cpp` | Suspect - token rewrite (TRUE 2->0, FALSE 1->0). |
| `src/dashboard/dashboard_window_chrome.cpp` | Suspect - token rewrite (TRUE 1->0, FALSE 2->0). |
| `src/dashboard/dashboard_window_chrome.h` | OK. |
| `src/dashboard/display_placement_menu_bitmap.cpp` | Suspect - token rewrite (FALSE 1->0). |
| `src/dashboard/display_placement_menu_bitmap.h` | OK. |
| `src/dashboard/fps_service.cpp` | Suspect - token rewrite (TRUE 2->1, FALSE 4->1). |
| `src/dashboard/native_theme_colors.cpp` | OK. |
| `src/dashboard_renderer/dashboard_renderer.cpp` | OK. |
| `src/dashboard_renderer/dashboard_renderer.h` | OK. |
| `src/dashboard_renderer/impl/animation_timeline.cpp` | OK. |
| `src/dashboard_renderer/impl/animation_timeline.h` | OK. |
| `src/dashboard_renderer/impl/dashboard_renderer_benchmark.cpp` | OK. |
| `src/dashboard_renderer/impl/dashboard_renderer_benchmark.h` | OK. |
| `src/dashboard_renderer/impl/layout_edit_overlay_renderer.cpp` | Suspect - initializer padding: lines 1257-1258: `LayoutContainerEditKey{ owner... }`. |
| `src/dashboard_renderer/impl/layout_edit_overlay_renderer.h` | OK. |
| `src/dashboard_renderer/impl/layout_resolver.cpp` | Suspect - initializer padding: lines 514-515: `LayoutContainerEditKey{ owner... }`. |
| `src/dashboard_renderer/impl/layout_resolver.h` | OK. |
| `src/dashboard_renderer/impl/metric_lookup_cache.cpp` | OK. |
| `src/dashboard_renderer/impl/render_thread.cpp` | Suspect - token rewrite (TRUE 3->0, FALSE 3->0). |
| `src/dashboard_renderer/impl/render_thread.h` | OK. |
| `src/dashboard_renderer/layout_guide_sheet_support.cpp` | OK. |
| `src/dashboard_renderer/layout_guide_sheet_support.h` | OK. |
| `src/diagnostics/crash_report.cpp` | Suspect - token rewrite (FALSE 2->0). |
| `src/diagnostics/diagnostics.cpp` | OK. |
| `src/diagnostics/diagnostics.h` | OK. |
| `src/diagnostics/snapshot_dump.cpp` | OK. |
| `src/display/display_config.cpp` | OK. |
| `src/display/display_config.h` | OK. |
| `src/display/monitor.cpp` | Suspect - token rewrite (TRUE 3->0, FALSE 2->0). |
| `src/display/monitor.h` | OK. |
| `src/headless/layout_guide_sheet_output.cpp` | OK. |
| `src/headless/layout_guide_sheet_output.h` | OK. |
| `src/layout_edit/impl/layout_snap_solver.cpp` | OK. |
| `src/layout_edit/impl/layout_snap_solver.h` | OK. |
| `src/layout_edit/layout_edit_controller.cpp` | OK. |
| `src/layout_edit/layout_edit_controller.h` | OK. |
| `src/layout_edit/layout_edit_hit_test.cpp` | OK. |
| `src/layout_edit/layout_edit_hit_test.h` | OK. |
| `src/layout_edit/layout_edit_parameter_edit.cpp` | OK. |
| `src/layout_edit/layout_edit_parameter_edit.h` | OK. |
| `src/layout_edit/layout_edit_service.cpp` | OK. |
| `src/layout_edit/layout_edit_service.h` | OK. |
| `src/layout_edit/layout_edit_target_descriptor.cpp` | OK. |
| `src/layout_edit/layout_edit_target_descriptor.h` | OK. |
| `src/layout_edit/layout_edit_tooltip.cpp` | OK. |
| `src/layout_edit/layout_edit_tooltip.h` | OK. |
| `src/layout_edit/layout_edit_tooltip_payload.cpp` | OK. |
| `src/layout_edit/layout_edit_tooltip_text.cpp` | OK. |
| `src/layout_edit/layout_edit_tooltip_text.h` | OK. |
| `src/layout_edit/layout_edit_trace_session.cpp` | OK. |
| `src/layout_edit/layout_edit_tree.cpp` | OK. |
| `src/layout_edit_dialog/impl/dialog_proc.cpp` | Suspect - token rewrite (TRUE 35->0, FALSE 4->0). |
| `src/layout_edit_dialog/impl/editors.cpp` | Suspect - token rewrite (TRUE 15->0, FALSE 10->0). |
| `src/layout_edit_dialog/impl/pane.cpp` | Suspect - token rewrite (TRUE 8->0, FALSE 6->0). |
| `src/layout_edit_dialog/impl/pane.h` | OK. |
| `src/layout_edit_dialog/impl/tree.cpp` | Suspect - ternary spacing: line 247: `TreeViewportSnapshot{}: CaptureTreeViewportSnapshot(...)`. |
| `src/layout_edit_dialog/impl/tree.h` | OK. |
| `src/layout_edit_dialog/impl/util.cpp` | Suspect - token rewrite (TRUE 16->0, FALSE 1->0). |
| `src/layout_edit_dialog/impl/util.h` | OK. |
| `src/layout_edit_dialog/layout_edit_dialog.cpp` | Suspect - token rewrite (TRUE 1->0, FALSE 7->0). |
| `src/layout_edit_dialog/layout_edit_dialog.h` | OK. |
| `src/layout_edit_dialog/theme_preview.cpp` | OK. |
| `src/layout_guide_sheet/impl/layout_guide_sheet_placement.cpp` | OK. |
| `src/layout_guide_sheet/impl/layout_guide_sheet_placement.h` | OK. |
| `src/layout_guide_sheet/impl/layout_guide_sheet_planner.cpp` | OK. |
| `src/layout_guide_sheet/impl/layout_guide_sheet_planner.h` | OK. |
| `src/layout_guide_sheet/impl/layout_guide_sheet_renderer.cpp` | Suspect - ternary spacing: lines 112-113: `std::string_view{}: std::string_view{...}`. |
| `src/layout_guide_sheet/impl/layout_guide_sheet_renderer.h` | OK. |
| `src/layout_guide_sheet/layout_guide_sheet.cpp` | OK. |
| `src/layout_guide_sheet/layout_guide_sheet.h` | OK. |
| `src/layout_model/dashboard_overlay_state.cpp` | OK. |
| `src/layout_model/layout_edit_active_region.cpp` | OK. |
| `src/layout_model/layout_edit_active_region.h` | OK. |
| `src/layout_model/layout_edit_anchor_shape.cpp` | OK. |
| `src/layout_model/layout_edit_anchor_shape.h` | OK. |
| `src/layout_model/layout_edit_helpers.cpp` | OK. |
| `src/layout_model/layout_edit_helpers.h` | OK. |
| `src/layout_model/layout_edit_parameter_metadata.cpp` | OK. |
| `src/layout_model/layout_edit_parameter_metadata.h` | OK. |
| `src/layout_model/layout_edit_service.cpp` | OK. |
| `src/layout_model/layout_edit_service.h` | OK. |
| `src/main/main.cpp` | OK. |
| `src/renderer/impl/d2d_cache.cpp` | OK. |
| `src/renderer/impl/d2d_render_conversions.cpp` | OK. |
| `src/renderer/impl/d2d_render_conversions.h` | OK. |
| `src/renderer/impl/d2d_renderer.cpp` | Suspect - token rewrite (TRUE 1->0, FALSE 1->0). |
| `src/renderer/impl/d2d_renderer.h` | OK. |
| `src/renderer/impl/palette.cpp` | OK. |
| `src/renderer/layout_guide_sheet_palette.cpp` | OK. |
| `src/renderer/png_export.cpp` | OK. |
| `src/renderer/png_export.h` | OK. |
| `src/renderer/render_types.cpp` | OK. |
| `src/renderer/render_types.h` | OK. |
| `src/renderer/renderer.h` | OK. |
| `src/telemetry/board/asus/board_asus_armoury_crate.cpp` | Suspect - token rewrite (FALSE 2->0). |
| `src/telemetry/board/board_vendor.cpp` | OK. |
| `src/telemetry/board/gigabyte/board_gigabyte_siv.cpp` | OK. |
| `src/telemetry/board/gigabyte/board_gigabyte_siv_bridge.cpp` | OK. |
| `src/telemetry/board/lenovo/board_lenovo_vantage.cpp` | Suspect - token rewrite (FALSE 1->0). |
| `src/telemetry/board/msi/board_msi_center_bridge.cpp` | OK. |
| `src/telemetry/fps/fps_etw_provider.cpp` | Suspect - token rewrite (FALSE 1->0). |
| `src/telemetry/fps/fps_service_client_provider.cpp` | OK. |
| `src/telemetry/fps/fps_service_client_provider.h` | OK. |
| `src/telemetry/fps_service_protocol.cpp` | OK. |
| `src/telemetry/fps_service_protocol.h` | OK. |
| `src/telemetry/gpu/amd/gpu_amd_adl.cpp` | OK. |
| `src/telemetry/gpu/amd/gpu_amd_adl.h` | OK. |
| `src/telemetry/gpu/gpu_vendor.cpp` | OK. |
| `src/telemetry/gpu/gpu_vendor_selection.cpp` | OK. |
| `src/telemetry/gpu/intel/gpu_intel_level_zero.cpp` | Suspect - macro decltype loss: `decltype(function)` became `decltype` in `CASEDASH_LOAD_OPTIONAL`; ternary spacing: line 307: `std::string{}: value`. |
| `src/telemetry/gpu/intel/gpu_intel_level_zero.h` | OK. |
| `src/telemetry/gpu/nvidia/gpu_nvidia_nvml.cpp` | Suspect - macro decltype loss: `decltype(function)` became `decltype` in loader macros. |
| `src/telemetry/gpu/nvidia/gpu_nvidia_nvml.h` | OK. |
| `src/telemetry/impl/collector.cpp` | OK. |
| `src/telemetry/impl/collector.h` | OK. |
| `src/telemetry/impl/collector_board.cpp` | OK. |
| `src/telemetry/impl/collector_cpu.cpp` | Suspect - token rewrite (FALSE 1->0). |
| `src/telemetry/impl/collector_fake.cpp` | OK. |
| `src/telemetry/impl/collector_gpu.cpp` | OK. |
| `src/telemetry/impl/collector_network.cpp` | Suspect - token rewrite (FALSE 2->0). |
| `src/telemetry/impl/collector_real.cpp` | OK. |
| `src/telemetry/impl/collector_storage.cpp` | Suspect - token rewrite (FALSE 1->0). |
| `src/telemetry/impl/collector_support.cpp` | OK. |
| `src/telemetry/impl/retained_history.cpp` | OK. |
| `src/telemetry/impl/system_info_support.cpp` | OK. |
| `src/telemetry/impl/system_info_support.h` | OK. |
| `src/telemetry/metrics.cpp` | OK. |
| `src/telemetry/telemetry.cpp` | Suspect - token rewrite (FALSE 2->0). |
| `src/telemetry/telemetry.h` | OK. |
| `src/tools/impl/format_break_model.cpp` | OK. |
| `src/tools/impl/format_include_sort.h` | OK. |
| `src/tools/impl/lint_checkers.cpp` | OK. |
| `src/tools/impl/lint_json.cpp` | OK. |
| `src/util/command_line.cpp` | OK. |
| `src/util/elevated_process.cpp` | OK. |
| `src/util/elevated_process.h` | OK. |
| `src/util/enum_string.h` | OK. |
| `src/util/function_ref.h` | OK. |
| `src/util/resource_loader.cpp` | OK. |
| `src/util/resource_strings.cpp` | OK. |
| `src/util/resource_strings.h` | OK. |
| `src/util/trace.cpp` | OK. |
| `src/util/trace.h` | OK. |
| `src/util/trace_timing.cpp` | Suspect - ternary spacing: line 120: `std::chrono::nanoseconds{}: HighPrecisionTimer::Elapsed(...)`. |
| `src/util/trace_timing.h` | OK. |
| `src/util/win32_format.cpp` | OK. |
| `src/widget/app_icon_geometry.cpp` | OK. |
| `src/widget/card_chrome_layout.cpp` | OK. |
| `src/widget/card_chrome_layout.h` | OK. |
| `src/widget/impl/animation_primitives.cpp` | OK. |
| `src/widget/impl/card_chrome.cpp` | OK. |
| `src/widget/impl/clock_date.cpp` | OK. |
| `src/widget/impl/clock_time.cpp` | OK. |
| `src/widget/impl/drive_usage_list.cpp` | OK. |
| `src/widget/impl/gauge.cpp` | OK. |
| `src/widget/impl/metric_list.cpp` | OK. |
| `src/widget/impl/network_footer.cpp` | OK. |
| `src/widget/impl/pill_bar.cpp` | OK. |
| `src/widget/impl/pill_bar.h` | OK. |
| `src/widget/impl/text.cpp` | OK. |
| `src/widget/impl/throughput.cpp` | OK. |
| `src/widget/layout_edit_types.cpp` | OK. |
| `src/widget/layout_edit_types.h` | OK. |
| `src/widget/widget.cpp` | OK. |
| `src/widget/widget.h` | OK. |
| `src/widget/widget_host.cpp` | OK. |
| `src/widget/widget_host.h` | OK. |
| `tests/animation_timeline_tests.cpp` | OK. |
| `tests/benchmarks.cpp` | Suspect - token rewrite (TRUE 2->0, FALSE 2->0). |
| `tests/config_meta_tests.cpp` | OK. |
| `tests/config_parser_tests.cpp` | OK. |
| `tests/config_resolution_tests.cpp` | OK. |
| `tests/config_writer_tests.cpp` | OK. |
| `tests/dashboard_titlebar_tests.cpp` | OK. |
| `tests/display_menu_options_tests.cpp` | OK - `kBitmapSize* kBitmapSize` matches the documented parenthesized-initializer ambiguity. |
| `tests/drive_usage_list_widget_tests.cpp` | OK. |
| `tests/enum_string_tests.cpp` | OK. |
| `tests/font_anchor_tests.cpp` | OK. |
| `tests/hardware_vendor_selection_tests.cpp` | OK. |
| `tests/layout_edit_controller_tests.cpp` | OK. |
| `tests/layout_edit_hit_test_tests.cpp` | Suspect - initializer padding: lines 217-220: `RenderPoint{ x, y }` and variants. |
| `tests/layout_edit_parameter_apply_tests.cpp` | OK. |
| `tests/layout_edit_tooltip_tests.cpp` | OK. |
| `tests/layout_edit_tree_tests.cpp` | OK. |
| `tests/layout_edit_types_tests.cpp` | OK. |
| `tests/layout_guide_sheet_planner_tests.cpp` | OK. |
| `tests/layout_snap_solver_tests.cpp` | OK. |
| `tests/localization_catalog_tests.cpp` | OK. |
| `tests/metric_list_widget_tests.cpp` | OK. |
| `tests/metrics_tests.cpp` | OK. |
| `tests/render_thread_tests.cpp` | OK. |
| `tests/resource_loader_test_stub.cpp` | OK. |
| `tests/snapshot_dump_tests.cpp` | OK. |
| `tests/telemetry_runtime_tests.cpp` | Suspect - token rewrite (TRUE 1->0, FALSE 1->0). |
| `tests/throughput_chart_widget_tests.cpp` | OK. |
| `tests/widget_class_tests.cpp` | OK. |

Coverage audit: every formatter-touched path from `git diff --name-only`, excluding this review log, is represented in the table above.
