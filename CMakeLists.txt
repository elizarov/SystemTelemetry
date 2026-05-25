cmake_minimum_required(VERSION 3.26)

project(CaseDash LANGUAGES C CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "CaseDash currently supports MSVC builds only.")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(cmake/CaseDashVersion.cmake)
casedash_configure_version_metadata()
message(STATUS "Configuring CaseDash ${CASEDASH_VERSION}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CASEDASH_LINK_MAPS "Emit linker map files for CaseDash size analysis." OFF)

foreach(cxx_flags_var IN ITEMS
    CMAKE_CXX_FLAGS
    CMAKE_CXX_FLAGS_DEBUG
    CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS_MINSIZEREL
)
    string(REPLACE "/EHsc" "" ${cxx_flags_var} "${${cxx_flags_var}}")
    # Size: source-local /Ob0 cold paths should not emit override warnings from CMake's default Release /Ob2.
    string(REPLACE "/Ob2" "" ${cxx_flags_var} "${${cxx_flags_var}}")
endforeach()

set(output_root "${CMAKE_SOURCE_DIR}/build")

foreach(config_name IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config_name}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${output_root}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${output_root}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${output_root}")
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${config_upper} "${output_root}")
endforeach()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${output_root}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${output_root}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${output_root}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${output_root}")

set(mscoree_search_paths)

if(DEFINED ENV{NETFXSDKDir})
    list(APPEND mscoree_search_paths
        "$ENV{NETFXSDKDir}/Lib/um/x64"
    )
endif()

if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKLibVersion})
    list(APPEND mscoree_search_paths
        "$ENV{WindowsSdkDir}/Lib/$ENV{WindowsSDKLibVersion}/um/x64"
    )
endif()

if(DEFINED ENV{WindowsSdkDir})
    list(APPEND mscoree_search_paths
        "$ENV{WindowsSdkDir}/Lib/winv6.3/um/x64"
    )
endif()

find_library(MSCOREE_LIB
    NAMES mscoree
    PATHS ${mscoree_search_paths}
)

if(NOT MSCOREE_LIB)
    message(FATAL_ERROR "mscoree.lib was not found. Install the .NET Framework SDK through Visual Studio Build Tools 2022 and run build.cmd from a configured developer prompt.")
endif()

set(CASEDASH_COMPRESSED_RESOURCE_RC "${CASEDASH_GENERATED_DIR}/CaseDashCompressedResources.rc")
set(CASEDASH_COMPRESSED_RESOURCE_DIR "${CASEDASH_GENERATED_DIR}/compressed_resources")
set(CASEDASH_APP_COMPRESSED_RESOURCE_RC "${CASEDASH_GENERATED_DIR}/CaseDashAppCompressedResources.rc")
set(CASEDASH_APP_COMPRESSED_RESOURCE_DIR "${CASEDASH_GENERATED_DIR}/compressed_resources_app")
set(CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_RC
    "${CASEDASH_GENERATED_DIR}/CaseDashHeadlessLayoutGuideSheetConfig.rc")
set(CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_DIR
    "${CASEDASH_GENERATED_DIR}/layout_guide_sheet_config")
set(CASEDASH_RESOURCE_STRING_HEADER "${CASEDASH_GENERATED_DIR}/resource_strings.generated.h")
file(GLOB_RECURSE CASEDASH_RESOURCE_STRING_SCAN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)
set(CASEDASH_APP_RESOURCE_STRING_SCAN_SOURCES ${CASEDASH_RESOURCE_STRING_SCAN_SOURCES})
list(FILTER CASEDASH_APP_RESOURCE_STRING_SCAN_SOURCES EXCLUDE REGEX
    "[/\\\\]src[/\\\\](headless|layout_guide_sheet)[/\\\\]"
)
add_custom_command(
    OUTPUT
        "${CASEDASH_COMPRESSED_RESOURCE_RC}"
        "${CASEDASH_COMPRESSED_RESOURCE_DIR}/text_atlas.cdlz"
        "${CASEDASH_COMPRESSED_RESOURCE_DIR}/resource_strings.txt"
        "${CASEDASH_RESOURCE_STRING_HEADER}"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/compress_resources.py"
        --output-dir "${CASEDASH_COMPRESSED_RESOURCE_DIR}"
        --output-rc "${CASEDASH_COMPRESSED_RESOURCE_RC}"
        --resource-root "${CMAKE_SOURCE_DIR}/resources"
        --resource-string-header "${CASEDASH_RESOURCE_STRING_HEADER}"
        --source-root "${CMAKE_SOURCE_DIR}"
        --exclude-config-section "layout_guide_sheet"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/compress_resources.py"
        "${CMAKE_SOURCE_DIR}/resources/config.ini"
        "${CMAKE_SOURCE_DIR}/resources/localization.ini"
        ${CASEDASH_RESOURCE_STRING_SCAN_SOURCES}
    VERBATIM
)
set_source_files_properties("${CASEDASH_COMPRESSED_RESOURCE_RC}" PROPERTIES
    OBJECT_DEPENDS "${CASEDASH_COMPRESSED_RESOURCE_DIR}/text_atlas.cdlz"
)
add_custom_target(CaseDashGeneratedResources DEPENDS
    "${CASEDASH_COMPRESSED_RESOURCE_RC}"
    "${CASEDASH_COMPRESSED_RESOURCE_DIR}/text_atlas.cdlz"
    "${CASEDASH_COMPRESSED_RESOURCE_DIR}/resource_strings.txt"
    "${CASEDASH_RESOURCE_STRING_HEADER}"
)

add_custom_command(
    OUTPUT
        "${CASEDASH_APP_COMPRESSED_RESOURCE_RC}"
        "${CASEDASH_APP_COMPRESSED_RESOURCE_DIR}/text_atlas.cdlz"
        "${CASEDASH_APP_COMPRESSED_RESOURCE_DIR}/resource_strings.txt"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/compress_resources.py"
        --output-dir "${CASEDASH_APP_COMPRESSED_RESOURCE_DIR}"
        --output-rc "${CASEDASH_APP_COMPRESSED_RESOURCE_RC}"
        --resource-root "${CMAKE_SOURCE_DIR}/resources"
        --source-root "${CMAKE_SOURCE_DIR}"
        --exclude-config-section "layout_guide_sheet"
        --exclude-source-prefix "src/headless"
        --exclude-source-prefix "src/layout_guide_sheet"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/compress_resources.py"
        "${CMAKE_SOURCE_DIR}/resources/config.ini"
        "${CMAKE_SOURCE_DIR}/resources/localization.ini"
        "${CASEDASH_RESOURCE_STRING_HEADER}"
        ${CASEDASH_APP_RESOURCE_STRING_SCAN_SOURCES}
    VERBATIM
)
set_source_files_properties("${CASEDASH_APP_COMPRESSED_RESOURCE_RC}" PROPERTIES
    OBJECT_DEPENDS "${CASEDASH_APP_COMPRESSED_RESOURCE_DIR}/text_atlas.cdlz"
)
add_custom_target(CaseDashGeneratedAppResources DEPENDS
    "${CASEDASH_APP_COMPRESSED_RESOURCE_RC}"
    "${CASEDASH_APP_COMPRESSED_RESOURCE_DIR}/text_atlas.cdlz"
    "${CASEDASH_APP_COMPRESSED_RESOURCE_DIR}/resource_strings.txt"
)

add_custom_command(
    OUTPUT
        "${CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_RC}"
        "${CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_DIR}/layout_guide_sheet.ini"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/extract_config_section.py"
        --input "${CMAKE_SOURCE_DIR}/resources/config.ini"
        --section "layout_guide_sheet"
        --output-ini "${CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_DIR}/layout_guide_sheet.ini"
        --output-rc "${CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_RC}"
        --resource-id "IDR_LAYOUT_GUIDE_SHEET_CONFIG"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/extract_config_section.py"
        "${CMAKE_SOURCE_DIR}/resources/config.ini"
    VERBATIM
)
set_source_files_properties("${CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_RC}" PROPERTIES
    OBJECT_DEPENDS "${CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_DIR}/layout_guide_sheet.ini"
)
add_custom_target(CaseDashGeneratedHeadlessLayoutGuideSheetConfig DEPENDS
    "${CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_RC}"
    "${CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_DIR}/layout_guide_sheet.ini"
)

set(CASEDASH_CONFIG_META_DIR "${CASEDASH_GENERATED_DIR}/config")
set(CASEDASH_CONFIG_META_CPP "${CASEDASH_CONFIG_META_DIR}/config_meta.generated.cpp")
set(CASEDASH_CONFIG_META_JSON "${CASEDASH_CONFIG_META_DIR}/config_meta.generated.json")
set(CASEDASH_LAYOUT_EDIT_META_CPP "${CASEDASH_GENERATED_DIR}/layout_model/layout_edit_parameter_metadata.generated.cpp")
add_custom_command(
    OUTPUT
        "${CASEDASH_CONFIG_META_CPP}"
        "${CASEDASH_LAYOUT_EDIT_META_CPP}"
        "${CASEDASH_CONFIG_META_JSON}"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/config_meta_gen.py"
        --descriptor "${CMAKE_SOURCE_DIR}/src/config/config.h"
        --primitives "${CMAKE_SOURCE_DIR}/src/config/config_primitives.h"
        --layout-edit-parameters "${CMAKE_SOURCE_DIR}/src/widget/layout_edit_parameter_id.h"
        --resource-config "${CMAKE_SOURCE_DIR}/resources/config.ini"
        --output-cpp "${CASEDASH_CONFIG_META_CPP}"
        --output-layout-edit-cpp "${CASEDASH_LAYOUT_EDIT_META_CPP}"
        --output-json "${CASEDASH_CONFIG_META_JSON}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/config_meta_gen.py"
        "${CMAKE_SOURCE_DIR}/src/config/config.h"
        "${CMAKE_SOURCE_DIR}/src/config/config_primitives.h"
        "${CMAKE_SOURCE_DIR}/src/widget/layout_edit_parameter_id.h"
        "${CMAKE_SOURCE_DIR}/resources/config.ini"
    VERBATIM
)
add_custom_target(CaseDashGeneratedConfigMeta DEPENDS
    "${CASEDASH_CONFIG_META_CPP}"
    "${CASEDASH_LAYOUT_EDIT_META_CPP}"
    "${CASEDASH_CONFIG_META_JSON}"
)

add_executable(CaseDash WIN32
    resources/CaseDash.rc
    ${CASEDASH_APP_COMPRESSED_RESOURCE_RC}
    ${CASEDASH_CONFIG_META_CPP}
    ${CASEDASH_LAYOUT_EDIT_META_CPP}
    ${CASEDASH_VERSION_RC}
    src/main/main.cpp
    src/diagnostics/diagnostics.cpp
    src/diagnostics/crash_report.cpp
    src/dashboard/autostart.cpp
    src/diagnostics/app_icon_export.cpp
    src/dashboard/fps_service.cpp
    src/util/command_line.cpp
    src/widget/app_icon_geometry.cpp
    src/config/config_io.cpp
    src/config/diagnostics_options.cpp
    src/display/display_config.cpp
    src/display/monitor.cpp
    src/util/elevated_process.cpp
    src/util/file_path.cpp
    src/util/message_box.cpp
    src/util/numeric_format.cpp
    src/util/text_format.cpp
    src/util/paths.cpp
    src/util/resource_strings.cpp
    src/util/scale.cpp
    src/util/lightweight_mutex.cpp
    src/util/strings.cpp
    src/util/temp_file.cpp
    src/util/win32_format.cpp
    src/dashboard/dashboard_controller.cpp
    src/dashboard/dashboard_app.cpp
    src/dashboard/dashboard_menu_format.cpp
    src/dashboard/dashboard_shell_ui.cpp
    src/layout_edit_dialog/layout_edit_dialog.cpp
    src/layout_edit_dialog/theme_preview.cpp
    src/layout_edit_dialog/impl/dialog_proc.cpp
    src/layout_edit_dialog/impl/editors.cpp
    src/layout_edit_dialog/impl/editor_handler_registry.cpp
    src/layout_edit_dialog/impl/pane.cpp
    src/layout_edit_dialog/impl/state.cpp
    src/layout_edit_dialog/impl/tree.cpp
    src/layout_edit_dialog/impl/util.cpp
    src/config/metric_board_binding.cpp
    src/layout_edit/layout_edit_hit_test.cpp
    src/layout_edit/layout_edit_controller.cpp
    src/layout_model/layout_edit_active_region.cpp
    src/layout_model/layout_edit_helpers.cpp
    src/layout_model/layout_edit_hit_priority.cpp
    src/layout_model/layout_edit_layout_target.cpp
    src/layout_model/layout_edit_service.cpp
    src/layout_edit/layout_edit_tree.cpp
    src/layout_model/layout_edit_parameter_metadata.cpp
    src/layout_edit/layout_edit_tooltip.cpp
    src/layout_edit/layout_edit_parameter_edit.cpp
    src/layout_edit/layout_edit_target_descriptor.cpp
    src/layout_edit/layout_edit_service.cpp
    src/layout_edit/layout_edit_tooltip_payload.cpp
    src/layout_edit/layout_edit_tooltip_text.cpp
    src/config/config_telemetry.cpp
    src/config/config_primitives.cpp
    src/config/color_format.cpp
    src/config/color_expression.cpp
    src/config/color_math.cpp
    src/config/color_resolver.cpp
    src/config/config_file_io.cpp
    src/config/metric_catalog.cpp
    src/config/config_parser.cpp
    src/config/config_runtime_fields.cpp
    src/config/config_writer.cpp
    src/config/config_resolution.cpp
    src/util/localization_catalog.cpp
    src/util/resource_loader.cpp
    src/util/lightweight_mutex.cpp
    src/layout_edit/impl/layout_snap_solver.cpp
    src/util/numeric_safety.cpp
    src/telemetry/impl/system_info_support.cpp
    src/telemetry/telemetry.cpp
    src/telemetry/impl/collector.cpp
    src/telemetry/impl/collector_state.cpp
    src/telemetry/impl/collector_board.cpp
    src/telemetry/impl/collector_cpu.cpp
    src/telemetry/impl/collector_gpu.cpp
    src/telemetry/impl/collector_support.cpp
    src/telemetry/impl/retained_history.cpp
    src/telemetry/impl/collector_storage.cpp
    src/telemetry/impl/collector_storage_selection.cpp
    src/telemetry/impl/collector_network.cpp
    src/telemetry/impl/collector_real.cpp
    src/telemetry/impl/collector_fake.cpp
    src/diagnostics/snapshot_dump.cpp
    src/telemetry/fps_provider.cpp
    src/telemetry/fps/impl/gpu_raw_counter_map.cpp
    src/telemetry/fps/fps_etw_provider.cpp
    src/telemetry/fps/fps_service_client_provider.cpp
    src/telemetry/fps_service_protocol.cpp
    src/telemetry/gpu/gpu_vendor.cpp
    src/telemetry/gpu/gpu_vendor_selection.cpp
    src/telemetry/gpu/intel/gpu_intel_level_zero.cpp
    src/telemetry/gpu/nvidia/gpu_nvidia_nvml.cpp
    src/telemetry/gpu/amd/gpu_amd_adl.cpp
    src/telemetry/board/board_vendor.cpp
    src/telemetry/board/board_vendor_selection.cpp
    src/telemetry/board/asus/board_asus_armoury_crate.cpp
    src/telemetry/board/gigabyte/board_gigabyte_siv.cpp
    src/telemetry/board/msi/board_msi_center.cpp
    src/telemetry/metrics.cpp
    src/layout_model/dashboard_overlay_state.cpp
    src/widget/card_chrome_layout.cpp
    src/renderer/renderer.cpp
    src/renderer/png_export.cpp
    src/renderer/render_types.cpp
    src/renderer/impl/d2d_renderer.cpp
    src/renderer/impl/d2d_cache.cpp
    src/renderer/impl/d2d_render_conversions.cpp
    src/layout_model/layout_edit_anchor_shape.cpp
    src/dashboard_renderer/impl/metric_lookup_cache.cpp
    src/dashboard_renderer/impl/animation_timeline.cpp
    src/dashboard_renderer/impl/layout_edit_overlay_renderer.cpp
    src/dashboard_renderer/impl/render_thread.cpp
    src/renderer/impl/palette.cpp
    src/renderer/impl/text_width_cache.cpp
    src/widget/animation.cpp
    src/widget/animation_types.cpp
    src/widget/widget.cpp
    src/widget/widget_host.cpp
    src/widget/layout_edit_types.cpp
    src/widget/impl/animation_primitives.cpp
    src/widget/impl/card_chrome.cpp
    src/widget/impl/pill_bar.cpp
    src/widget/impl/text.cpp
    src/widget/impl/gauge.cpp
    src/widget/impl/metric_list.cpp
    src/widget/impl/throughput.cpp
    src/widget/impl/network_footer.cpp
    src/widget/impl/vertical_spacer.cpp
    src/widget/impl/vertical_spring.cpp
    src/widget/impl/drive_usage_list.cpp
    src/widget/impl/clock_time.cpp
    src/widget/impl/clock_date.cpp
    src/dashboard_renderer/impl/layout_resolver.cpp
    src/dashboard_renderer/dashboard_renderer.cpp
    src/util/high_precision_timer.cpp
    src/util/text_encoding.cpp
    src/util/trace.cpp
    src/util/trace_timing.cpp
    src/vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    src/vendor/adlx/SDK/Platform/Windows/WinAPIs.cpp
)

set_source_files_properties(resources/CaseDash.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/app.ico"
)

set_source_files_properties(${CASEDASH_VERSION_RC} ${CASEDASH_BENCHMARKS_VERSION_RC} PROPERTIES
    OBJECT_DEPENDS "${CASEDASH_MANIFEST_PATH}"
)
set_source_files_properties(${CASEDASH_HEADLESS_VERSION_RC} PROPERTIES
    OBJECT_DEPENDS "${CASEDASH_HEADLESS_MANIFEST_PATH}"
)

set(casedash_release_speed_sources
    src/config/config_telemetry.cpp
    src/config/config_primitives.cpp
    src/config/config_resolution.cpp
    src/config/metric_catalog.cpp
    src/dashboard/dashboard_app.cpp
    src/dashboard/dashboard_controller.cpp
    src/dashboard/dashboard_shell_ui.cpp
    src/diagnostics/app_icon_export.cpp
    src/widget/app_icon_geometry.cpp
    src/dashboard_renderer/dashboard_renderer.cpp
    src/layout_model/layout_edit_anchor_shape.cpp
    src/dashboard_renderer/impl/metric_lookup_cache.cpp
    src/dashboard_renderer/impl/animation_timeline.cpp
    src/dashboard_renderer/impl/layout_edit_overlay_renderer.cpp
    src/dashboard_renderer/impl/render_thread.cpp
    src/dashboard_renderer/impl/layout_resolver.cpp
    src/layout_edit/impl/layout_snap_solver.cpp
    src/layout_edit/layout_edit_controller.cpp
    src/layout_edit/layout_edit_hit_test.cpp
    src/layout_edit/layout_edit_parameter_edit.cpp
    src/layout_edit/layout_edit_service.cpp
    src/layout_edit/layout_edit_target_descriptor.cpp
    src/layout_edit/layout_edit_tooltip.cpp
    src/layout_edit/layout_edit_tooltip_payload.cpp
    src/layout_edit/layout_edit_tooltip_text.cpp
    src/layout_edit/layout_edit_tree.cpp
    src/layout_edit_dialog/theme_preview.cpp
    src/layout_guide_sheet/layout_guide_sheet.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_placement.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_planner.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_renderer.cpp
    src/layout_model/dashboard_overlay_state.cpp
    src/layout_model/layout_edit_active_region.cpp
    src/layout_model/layout_edit_helpers.cpp
    src/layout_model/layout_edit_hit_priority.cpp
    src/layout_model/layout_edit_layout_target.cpp
    src/layout_model/layout_edit_parameter_metadata.cpp
    src/layout_model/layout_edit_service.cpp
    src/renderer/renderer.cpp
    src/renderer/png_export.cpp
    src/renderer/render_types.cpp
    src/renderer/impl/d2d_cache.cpp
    src/renderer/impl/d2d_render_conversions.cpp
    src/renderer/impl/d2d_renderer.cpp
    src/renderer/impl/palette.cpp
    src/renderer/impl/text_width_cache.cpp
    src/telemetry/board/board_vendor.cpp
    src/telemetry/board/board_vendor_selection.cpp
    src/telemetry/board/asus/board_asus_armoury_crate.cpp
    src/telemetry/board/gigabyte/board_gigabyte_siv.cpp
    src/telemetry/board/msi/board_msi_center.cpp
    src/telemetry/fps_provider.cpp
    src/telemetry/fps/impl/gpu_raw_counter_map.cpp
    src/telemetry/fps/fps_etw_provider.cpp
    src/telemetry/fps/fps_service_client_provider.cpp
    src/telemetry/fps_service_protocol.cpp
    src/telemetry/gpu/gpu_vendor.cpp
    src/telemetry/gpu/gpu_vendor_selection.cpp
    src/telemetry/gpu/intel/gpu_intel_level_zero.cpp
    src/telemetry/gpu/nvidia/gpu_nvidia_nvml.cpp
    src/telemetry/gpu/amd/gpu_amd_adl.cpp
    src/telemetry/impl/collector_board.cpp
    src/telemetry/impl/collector_cpu.cpp
    src/telemetry/impl/collector_fake.cpp
    src/telemetry/impl/collector_gpu.cpp
    src/telemetry/impl/collector_real.cpp
    src/telemetry/impl/collector_state.cpp
    src/telemetry/impl/collector_storage.cpp
    src/telemetry/impl/collector_storage_selection.cpp
    src/telemetry/impl/collector_support.cpp
    src/telemetry/impl/retained_history.cpp
    src/telemetry/impl/system_info_support.cpp
    src/telemetry/metrics.cpp
    src/telemetry/telemetry.cpp
    src/widget/animation.cpp
    src/widget/animation_types.cpp
    src/widget/card_chrome_layout.cpp
    src/widget/layout_edit_types.cpp
    src/widget/widget.cpp
    src/widget/widget_host.cpp
    src/widget/impl/animation_primitives.cpp
    src/widget/impl/card_chrome.cpp
    src/widget/impl/pill_bar.cpp
    src/widget/impl/clock_date.cpp
    src/widget/impl/clock_time.cpp
    src/widget/impl/drive_usage_list.cpp
    src/widget/impl/gauge.cpp
    src/widget/impl/metric_list.cpp
    src/widget/impl/network_footer.cpp
    src/widget/impl/text.cpp
    src/widget/impl/throughput.cpp
    src/widget/impl/vertical_spacer.cpp
    src/widget/impl/vertical_spring.cpp
    tests/benchmarks.cpp
)

set_source_files_properties(${casedash_release_speed_sources} PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:/O2>"
)

# Size: these retained cold/orchestration paths still measure smaller with inlining disabled under LTCG.
set(casedash_release_noinline_sources
    src/dashboard/dashboard_app.cpp
    src/dashboard/dashboard_controller.cpp
    src/diagnostics/app_icon_export.cpp
    src/diagnostics/crash_report.cpp
    src/diagnostics/diagnostics.cpp
    src/display/display_config.cpp
    src/layout_edit/layout_edit_tree.cpp
    src/main/main.cpp
    src/telemetry/impl/collector_fake.cpp
)

set_property(SOURCE ${casedash_release_noinline_sources} APPEND PROPERTY
    COMPILE_OPTIONS "$<$<CONFIG:Release>:/Ob0>"
)

add_library(CaseDashGigabyte OBJECT
    src/telemetry/board/gigabyte/board_gigabyte_siv_bridge.cpp
)

add_library(CaseDashMsi OBJECT
    src/telemetry/board/msi/board_msi_center_bridge.cpp
)

set_target_properties(CaseDashGigabyte PROPERTIES
    COMMON_LANGUAGE_RUNTIME ""
    VS_DOTNET_TARGET_FRAMEWORK_VERSION "v4.0"
)

set_target_properties(CaseDashMsi PROPERTIES
    COMMON_LANGUAGE_RUNTIME ""
    VS_DOTNET_TARGET_FRAMEWORK_VERSION "v4.0"
)

target_include_directories(CaseDashGigabyte PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
)

target_include_directories(CaseDashMsi PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
)

target_compile_definitions(CaseDashGigabyte PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _HAS_EXCEPTIONS=0
)

target_compile_definitions(CaseDashMsi PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _HAS_EXCEPTIONS=0
)

target_compile_options(CaseDashGigabyte PRIVATE
    /clr
    /W4
    /permissive-
    /w14505
    /we4505
    /Zi
)

target_compile_options(CaseDashMsi PRIVATE
    /clr
    /W4
    /permissive-
    /w14505
    /we4505
    /Zi
)

target_sources(CaseDash PRIVATE
    $<TARGET_OBJECTS:CaseDashGigabyte>
    $<TARGET_OBJECTS:CaseDashMsi>
)

target_include_directories(CaseDash PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
    "${CMAKE_SOURCE_DIR}/src/vendor/adlx"
    "${CASEDASH_GENERATED_DIR}"
)
add_dependencies(CaseDash CaseDashGeneratedResources CaseDashGeneratedAppResources CaseDashGeneratedConfigMeta)

target_compile_definitions(CaseDash PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _HAS_EXCEPTIONS=0
    _USE_STD_VECTOR_ALGORITHMS=0
)

target_compile_options(CaseDash PRIVATE
    /W4
    /wd4530
    /permissive-
    /GR-
    # Size: stack-cookie instrumentation is an explicit Release executable-size tradeoff; see docs/optimize_size.md.
    $<$<CONFIG:Release>:/GS->
    /w14505
    /we4505
    /Zi
    $<$<CONFIG:Release>:/Zc:threadSafeInit->
    $<$<CONFIG:Release>:/Os>
    $<$<CONFIG:Release>:/GL>
)

target_link_options(CaseDash PRIVATE
    $<$<CONFIG:Debug>:/DEBUG>
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/LTCG>
    # Size: the C++/CLI native-entry .nep thunk fits inside .text alignment slack.
    $<$<CONFIG:Release>:/MERGE:.nep=.text>
    $<$<CONFIG:Release>:/DYNAMICBASE:NO>
    $<$<AND:$<CONFIG:Release>,$<BOOL:${CASEDASH_LINK_MAPS}>>:/MAP:$<TARGET_FILE_DIR:CaseDash>/CaseDash.map>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL>
    /MANIFEST:NO
)

target_link_libraries(CaseDash PRIVATE
    user32
    gdi32
    d3d11
    d2d1
    dwrite
    pdh
    iphlpapi
    dxgi
    comctl32
    ws2_32
    ole32
    advapi32
    dbghelp
    wtsapi32
    ${MSCOREE_LIB}
)

set_target_properties(CaseDash PROPERTIES
    OUTPUT_NAME "CaseDash"
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)

add_executable(CaseDashHeadless
    ${CASEDASH_COMPRESSED_RESOURCE_RC}
    ${CASEDASH_HEADLESS_LAYOUT_GUIDE_SHEET_CONFIG_RC}
    ${CASEDASH_CONFIG_META_CPP}
    ${CASEDASH_LAYOUT_EDIT_META_CPP}
    ${CASEDASH_HEADLESS_VERSION_RC}
    src/headless/headless_main.cpp
    src/headless/layout_guide_sheet_output.cpp
    src/diagnostics/diagnostics.cpp
    src/diagnostics/crash_report.cpp
    src/diagnostics/app_icon_export.cpp
    src/util/command_line.cpp
    src/widget/app_icon_geometry.cpp
    src/config/config_io.cpp
    src/config/diagnostics_options.cpp
    src/util/elevated_process.cpp
    src/util/file_path.cpp
    src/util/numeric_format.cpp
    src/util/text_format.cpp
    src/util/paths.cpp
    src/util/resource_strings.cpp
    src/util/scale.cpp
    src/util/lightweight_mutex.cpp
    src/util/strings.cpp
    src/util/win32_format.cpp
    src/config/metric_board_binding.cpp
    src/layout_edit/layout_edit_hit_test.cpp
    src/layout_edit/layout_edit_controller.cpp
    src/layout_model/layout_edit_active_region.cpp
    src/layout_model/layout_edit_helpers.cpp
    src/layout_model/layout_edit_hit_priority.cpp
    src/layout_model/layout_edit_layout_target.cpp
    src/layout_model/layout_edit_service.cpp
    src/layout_edit/layout_edit_tree.cpp
    src/layout_model/layout_edit_parameter_metadata.cpp
    src/layout_edit/layout_edit_tooltip.cpp
    src/layout_edit/layout_edit_parameter_edit.cpp
    src/layout_edit/layout_edit_target_descriptor.cpp
    src/layout_edit/layout_edit_service.cpp
    src/layout_edit/layout_edit_tooltip_payload.cpp
    src/layout_edit/layout_edit_tooltip_text.cpp
    src/config/config_telemetry.cpp
    src/config/config_primitives.cpp
    src/config/color_format.cpp
    src/config/color_expression.cpp
    src/config/color_math.cpp
    src/config/color_resolver.cpp
    src/config/config_file_io.cpp
    src/config/metric_catalog.cpp
    src/config/config_parser.cpp
    src/config/config_runtime_fields.cpp
    src/config/config_writer.cpp
    src/config/config_resolution.cpp
    src/util/localization_catalog.cpp
    src/util/resource_loader.cpp
    src/layout_edit/impl/layout_snap_solver.cpp
    src/util/numeric_safety.cpp
    src/telemetry/impl/system_info_support.cpp
    src/telemetry/telemetry.cpp
    src/telemetry/impl/collector.cpp
    src/telemetry/impl/collector_state.cpp
    src/telemetry/impl/collector_board.cpp
    src/telemetry/impl/collector_cpu.cpp
    src/telemetry/impl/collector_gpu.cpp
    src/telemetry/impl/collector_support.cpp
    src/telemetry/impl/retained_history.cpp
    src/telemetry/impl/collector_storage.cpp
    src/telemetry/impl/collector_storage_selection.cpp
    src/telemetry/impl/collector_network.cpp
    src/telemetry/impl/collector_real.cpp
    src/telemetry/impl/collector_fake.cpp
    src/diagnostics/snapshot_dump.cpp
    src/telemetry/fps_provider.cpp
    src/telemetry/board_service_sample.cpp
    src/telemetry/fps/fps_etw_provider.cpp
    src/telemetry/fps/impl/gpu_raw_counter_map.cpp
    src/telemetry/fps/fps_service_client_provider.cpp
    src/telemetry/fps_service_protocol.cpp
    src/telemetry/gpu/gpu_vendor.cpp
    src/telemetry/gpu/gpu_vendor_selection.cpp
    src/telemetry/gpu/intel/gpu_intel_level_zero.cpp
    src/telemetry/gpu/nvidia/gpu_nvidia_nvml.cpp
    src/telemetry/gpu/amd/gpu_amd_adl.cpp
    src/telemetry/board/board_vendor.cpp
    src/telemetry/board/board_vendor_selection.cpp
    src/telemetry/board/asus/board_asus_armoury_crate.cpp
    src/telemetry/board/gigabyte/board_gigabyte_siv.cpp
    src/telemetry/board/lenovo/board_lenovo_vantage.cpp
    src/telemetry/board/msi/board_msi_center.cpp
    src/telemetry/metrics.cpp
    src/layout_model/dashboard_overlay_state.cpp
    src/widget/card_chrome_layout.cpp
    src/renderer/renderer.cpp
    src/renderer/png_export.cpp
    src/renderer/layout_guide_sheet_palette.cpp
    src/renderer/render_types.cpp
    src/renderer/impl/d2d_renderer.cpp
    src/renderer/impl/d2d_cache.cpp
    src/renderer/impl/d2d_render_conversions.cpp
    src/layout_model/layout_edit_anchor_shape.cpp
    src/dashboard_renderer/impl/metric_lookup_cache.cpp
    src/dashboard_renderer/impl/animation_timeline.cpp
    src/dashboard_renderer/impl/layout_edit_overlay_renderer.cpp
    src/dashboard_renderer/impl/render_thread.cpp
    src/renderer/impl/palette.cpp
    src/renderer/impl/text_width_cache.cpp
    src/widget/animation.cpp
    src/widget/animation_types.cpp
    src/widget/widget.cpp
    src/widget/widget_host.cpp
    src/widget/layout_edit_types.cpp
    src/widget/impl/animation_primitives.cpp
    src/widget/impl/card_chrome.cpp
    src/widget/impl/pill_bar.cpp
    src/widget/impl/text.cpp
    src/widget/impl/gauge.cpp
    src/widget/impl/metric_list.cpp
    src/widget/impl/throughput.cpp
    src/widget/impl/network_footer.cpp
    src/widget/impl/vertical_spacer.cpp
    src/widget/impl/vertical_spring.cpp
    src/widget/impl/drive_usage_list.cpp
    src/widget/impl/clock_time.cpp
    src/widget/impl/clock_date.cpp
    src/dashboard_renderer/impl/layout_resolver.cpp
    src/dashboard_renderer/layout_guide_sheet_support.cpp
    src/layout_guide_sheet/layout_guide_sheet.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_planner.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_placement.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_renderer.cpp
    src/dashboard_renderer/dashboard_renderer.cpp
    src/util/high_precision_timer.cpp
    src/util/text_encoding.cpp
    src/util/trace.cpp
    src/util/trace_timing.cpp
    src/vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    src/vendor/adlx/SDK/Platform/Windows/WinAPIs.cpp
)
target_sources(CaseDashHeadless PRIVATE
    $<TARGET_OBJECTS:CaseDashGigabyte>
    $<TARGET_OBJECTS:CaseDashMsi>
)
target_include_directories(CaseDashHeadless PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
    "${CMAKE_SOURCE_DIR}/src/vendor/adlx"
    "${CASEDASH_GENERATED_DIR}"
)
add_dependencies(CaseDashHeadless
    CaseDashGeneratedResources
    CaseDashGeneratedHeadlessLayoutGuideSheetConfig
    CaseDashGeneratedConfigMeta
)
target_compile_definitions(CaseDashHeadless PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _HAS_EXCEPTIONS=0
    _USE_STD_VECTOR_ALGORITHMS=0
    CASEDASH_ENABLE_LAYOUT_GUIDE_SHEET_OUTPUT=1
)
target_compile_options(CaseDashHeadless PRIVATE
    /W4
    /wd4530
    /permissive-
    /GR-
    $<$<CONFIG:Release>:/GS->
    /w14505
    /we4505
    /Zi
    $<$<CONFIG:Release>:/Zc:threadSafeInit->
    $<$<CONFIG:Release>:/Os>
    $<$<CONFIG:Release>:/GL>
)
target_link_options(CaseDashHeadless PRIVATE
    $<$<CONFIG:Debug>:/DEBUG>
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/LTCG>
    $<$<CONFIG:Release>:/MERGE:.nep=.text>
    $<$<CONFIG:Release>:/DYNAMICBASE:NO>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL>
    /SUBSYSTEM:CONSOLE
    /MANIFEST:NO
)
target_link_libraries(CaseDashHeadless PRIVATE
    user32
    gdi32
    d3d11
    d2d1
    dwrite
    pdh
    iphlpapi
    dxgi
    ws2_32
    ole32
    oleaut32
    wbemuuid
    advapi32
    dbghelp
    wtsapi32
    ${MSCOREE_LIB}
)
set_target_properties(CaseDashHeadless PROPERTIES
    OUTPUT_NAME "CaseDashHeadless"
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)

find_package(unofficial-tree-sitter CONFIG REQUIRED)

add_library(CaseDashTreeSitterCppGrammar STATIC
    src/tools/vendor/tree-sitter/tree-sitter-cpp/src/parser.c
    src/tools/vendor/tree-sitter/tree-sitter-cpp/src/scanner.c
)
target_include_directories(CaseDashTreeSitterCppGrammar
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src/tools/vendor/tree-sitter/tree-sitter-cpp/src"
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src/tools/vendor/tree-sitter/tree-sitter-cpp/include"
)
target_link_libraries(CaseDashTreeSitterCppGrammar PUBLIC
    unofficial::tree-sitter::tree-sitter
)
target_compile_options(CaseDashTreeSitterCppGrammar PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/w>
)

add_executable(CaseDashTools
    src/tools/tools_main.cpp
    src/tools/format.cpp
    src/tools/format_args.cpp
    src/tools/format_config.cpp
    src/tools/lint_check.cpp
    src/tools/impl/lint_checkers.cpp
    src/tools/impl/lint_common.cpp
    src/tools/impl/lint_json.cpp
)
target_include_directories(CaseDashTools PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(CaseDashTools PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _USE_STD_VECTOR_ALGORITHMS=0
)
target_compile_options(CaseDashTools PRIVATE
    /EHsc
    /W4
    /permissive-
    /GR-
    /w14505
    /we4505
    /Zi
    $<$<CONFIG:Release>:/O2>
)
target_link_options(CaseDashTools PRIVATE
    $<$<CONFIG:Debug>:/DEBUG>
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL>
)
target_link_libraries(CaseDashTools PRIVATE
    unofficial::tree-sitter::tree-sitter
    CaseDashTreeSitterCppGrammar
)
set_target_properties(CaseDashTools PROPERTIES
    OUTPUT_NAME "CaseDashTools"
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)

enable_testing()

set(casedash_vcpkg_triplet "x64-windows")
if(DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "")
    set(casedash_vcpkg_triplet "${VCPKG_TARGET_TRIPLET}")
endif()

set(casedash_vcpkg_installed_dir "")
if(DEFINED VCPKG_INSTALLED_DIR AND NOT VCPKG_INSTALLED_DIR STREQUAL "")
    set(casedash_vcpkg_installed_dir "${VCPKG_INSTALLED_DIR}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/${casedash_vcpkg_triplet}/share/gtest/GTestConfig.cmake")
    set(casedash_vcpkg_installed_dir "${CMAKE_SOURCE_DIR}/vcpkg")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/build/vcpkg_installed/${casedash_vcpkg_triplet}/share/gtest/GTestConfig.cmake")
    set(casedash_vcpkg_installed_dir "${CMAKE_SOURCE_DIR}/build/vcpkg_installed")
endif()

if(NOT casedash_vcpkg_installed_dir STREQUAL "" AND
   EXISTS "${casedash_vcpkg_installed_dir}/${casedash_vcpkg_triplet}/share/gtest/GTestConfig.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${casedash_vcpkg_installed_dir}/${casedash_vcpkg_triplet}")
endif()

find_package(GTest CONFIG REQUIRED)

add_executable(CaseDashTests
    ${CASEDASH_CONFIG_META_CPP}
    ${CASEDASH_LAYOUT_EDIT_META_CPP}
    tests/app_icon_renderer_tests.cpp
    tests/enum_string_tests.cpp
    tests/metrics_tests.cpp
    tests/snapshot_dump_tests.cpp
    tests/widget_factory_tests.cpp
    tests/widget_class_tests.cpp
    tests/font_anchor_tests.cpp
    tests/metric_list_widget_tests.cpp
    tests/card_chrome_layout_tests.cpp
    tests/layout_snap_solver_tests.cpp
    tests/board_metric_binding_tests.cpp
    tests/layout_edit_controller_tests.cpp
    tests/layout_edit_hit_test_tests.cpp
    tests/layout_edit_parameter_apply_tests.cpp
    tests/layout_edit_types_tests.cpp
    tests/layout_edit_tree_tests.cpp
    tests/layout_edit_tooltip_tests.cpp
    tests/layout_guide_sheet_planner_tests.cpp
    tests/localization_catalog_tests.cpp
    tests/config_meta_tests.cpp
    tests/config_parser_tests.cpp
    tests/config_resolution_tests.cpp
    tests/config_writer_tests.cpp
    tests/hardware_vendor_selection_tests.cpp
    tests/system_info_support_tests.cpp
    tests/fps_service_protocol_tests.cpp
    tests/telemetry_retained_history_tests.cpp
    tests/telemetry_runtime_tests.cpp
    tests/animation_timeline_tests.cpp
    tests/throughput_chart_widget_tests.cpp
    tests/render_thread_tests.cpp
    src/config/config_telemetry.cpp
    src/config/config_primitives.cpp
    src/config/color_format.cpp
    src/config/color_expression.cpp
    src/config/color_math.cpp
    src/config/color_resolver.cpp
    src/config/config_file_io.cpp
    src/config/metric_catalog.cpp
    src/config/config_parser.cpp
    src/config/config_runtime_fields.cpp
    src/config/config_writer.cpp
    src/config/config_resolution.cpp
    src/widget/app_icon_geometry.cpp
    src/layout_model/layout_edit_active_region.cpp
    src/layout_model/layout_edit_helpers.cpp
    src/layout_model/layout_edit_hit_priority.cpp
    src/layout_model/layout_edit_layout_target.cpp
    src/layout_model/layout_edit_service.cpp
    src/layout_model/dashboard_overlay_state.cpp
    src/config/metric_board_binding.cpp
    src/layout_edit/layout_edit_tree.cpp
    src/layout_model/layout_edit_parameter_metadata.cpp
    src/layout_edit/layout_edit_tooltip.cpp
    src/layout_edit/layout_edit_parameter_edit.cpp
    src/layout_edit/layout_edit_target_descriptor.cpp
    src/layout_edit/layout_edit_service.cpp
    src/layout_edit/layout_edit_tooltip_payload.cpp
    src/layout_edit/layout_edit_tooltip_text.cpp
    src/layout_edit/layout_edit_controller.cpp
    src/layout_edit/layout_edit_hit_test.cpp
    src/layout_edit_dialog/impl/editor_handler_registry.cpp
    src/util/strings.cpp
    src/util/numeric_format.cpp
    src/util/text_format.cpp
    src/util/localization_catalog.cpp
    src/util/resource_strings.cpp
    src/util/lightweight_mutex.cpp
    tests/resource_loader_test_stub.cpp
    src/layout_edit/impl/layout_snap_solver.cpp
    src/util/file_path.cpp
    src/util/numeric_safety.cpp
    src/util/win32_format.cpp
    src/telemetry/impl/system_info_support.cpp
    src/telemetry/board/board_vendor_selection.cpp
    src/telemetry/gpu/gpu_vendor_selection.cpp
    src/telemetry/telemetry.cpp
    src/telemetry/impl/collector.cpp
    src/telemetry/metrics.cpp
    src/widget/animation.cpp
    src/widget/animation_types.cpp
    src/widget/card_chrome_layout.cpp
    src/renderer/renderer.cpp
    src/renderer/png_export.cpp
    src/renderer/layout_guide_sheet_palette.cpp
    src/widget/widget.cpp
    src/widget/widget_host.cpp
    src/widget/layout_edit_types.cpp
    src/widget/impl/animation_primitives.cpp
    src/widget/impl/card_chrome.cpp
    src/widget/impl/pill_bar.cpp
    src/widget/impl/text.cpp
    src/widget/impl/gauge.cpp
    src/widget/impl/metric_list.cpp
    src/widget/impl/throughput.cpp
    src/widget/impl/network_footer.cpp
    src/widget/impl/vertical_spacer.cpp
    src/widget/impl/vertical_spring.cpp
    src/widget/impl/drive_usage_list.cpp
    src/widget/impl/clock_time.cpp
    src/widget/impl/clock_date.cpp
    src/renderer/render_types.cpp
    src/renderer/impl/d2d_renderer.cpp
    src/renderer/impl/d2d_cache.cpp
    src/renderer/impl/d2d_render_conversions.cpp
    src/renderer/impl/palette.cpp
    src/renderer/impl/text_width_cache.cpp
    src/layout_model/layout_edit_anchor_shape.cpp
    src/dashboard_renderer/impl/metric_lookup_cache.cpp
    src/dashboard_renderer/impl/animation_timeline.cpp
    src/dashboard_renderer/impl/layout_edit_overlay_renderer.cpp
    src/dashboard_renderer/impl/render_thread.cpp
    src/dashboard_renderer/impl/layout_resolver.cpp
    src/dashboard_renderer/layout_guide_sheet_support.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_placement.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_renderer.cpp
    src/dashboard_renderer/dashboard_renderer.cpp
    src/diagnostics/snapshot_dump.cpp
    src/diagnostics/app_icon_export.cpp
    src/layout_guide_sheet/layout_guide_sheet.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_planner.cpp
    src/telemetry/impl/collector_fake.cpp
    src/telemetry/impl/collector_storage_selection.cpp
    src/telemetry/impl/retained_history.cpp
    src/util/high_precision_timer.cpp
    src/util/text_encoding.cpp
    src/util/trace.cpp
    src/util/trace_timing.cpp
    src/telemetry/fps_service_protocol.cpp
  )
target_include_directories(CaseDashTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
    "${CASEDASH_GENERATED_DIR}"
)
add_dependencies(CaseDashTests CaseDashGeneratedResources CaseDashGeneratedConfigMeta)
target_compile_definitions(CaseDashTests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _USE_STD_VECTOR_ALGORITHMS=0
    CASEDASH_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CASEDASH_ENABLE_LAYOUT_GUIDE_SHEET_OUTPUT=1
)
target_compile_options(CaseDashTests PRIVATE
    /EHsc
    /W4
    /permissive-
    /GR-
    /w14505
    /we4505
    /Zi
)
target_link_options(CaseDashTests PRIVATE
    $<$<CONFIG:Debug>:/DEBUG>
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL>
)
target_link_libraries(CaseDashTests PRIVATE
    GTest::gtest_main
    user32
    gdi32
    d3d11
    d2d1
    dwrite
    ole32
)
set_target_properties(CaseDashTests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)
add_custom_command(TARGET CaseDashTests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:CaseDashTests>
        $<TARGET_FILE_DIR:CaseDashTests>
    COMMAND_EXPAND_LISTS
)

add_test(NAME CaseDashTests COMMAND CaseDashTests)

add_executable(CaseDashBenchmarks
    resources/CaseDash.rc
    ${CASEDASH_COMPRESSED_RESOURCE_RC}
    ${CASEDASH_CONFIG_META_CPP}
    ${CASEDASH_LAYOUT_EDIT_META_CPP}
    ${CASEDASH_BENCHMARKS_VERSION_RC}
    tests/benchmarks.cpp
    src/util/strings.cpp
    src/util/numeric_format.cpp
    src/util/text_format.cpp
    src/util/win32_format.cpp
    src/util/scale.cpp
    src/config/config_telemetry.cpp
    src/config/config_primitives.cpp
    src/config/color_format.cpp
    src/config/color_expression.cpp
    src/config/color_math.cpp
    src/config/color_resolver.cpp
    src/config/config_file_io.cpp
    src/config/metric_catalog.cpp
    src/config/config_parser.cpp
    src/config/config_runtime_fields.cpp
    src/config/config_resolution.cpp
    src/config/metric_board_binding.cpp
    src/telemetry/impl/system_info_support.cpp
    src/telemetry/telemetry.cpp
    src/telemetry/impl/collector.cpp
    src/telemetry/impl/collector_state.cpp
    src/telemetry/impl/collector_board.cpp
    src/telemetry/impl/collector_cpu.cpp
    src/telemetry/impl/collector_gpu.cpp
    src/telemetry/impl/collector_support.cpp
    src/telemetry/impl/retained_history.cpp
    src/telemetry/impl/collector_storage.cpp
    src/telemetry/impl/collector_storage_selection.cpp
    src/telemetry/impl/collector_network.cpp
    src/telemetry/impl/collector_real.cpp
    src/telemetry/impl/collector_fake.cpp
    src/diagnostics/snapshot_dump.cpp
    src/telemetry/fps_provider.cpp
    src/telemetry/fps/impl/gpu_raw_counter_map.cpp
    src/telemetry/fps/fps_etw_provider.cpp
    src/telemetry/fps/fps_service_client_provider.cpp
    src/telemetry/fps_service_protocol.cpp
    src/telemetry/gpu/gpu_vendor.cpp
    src/telemetry/gpu/gpu_vendor_selection.cpp
    src/telemetry/gpu/intel/gpu_intel_level_zero.cpp
    src/telemetry/gpu/nvidia/gpu_nvidia_nvml.cpp
    src/telemetry/gpu/amd/gpu_amd_adl.cpp
    src/telemetry/board/board_vendor.cpp
    src/telemetry/board/board_vendor_selection.cpp
    src/telemetry/board/asus/board_asus_armoury_crate.cpp
    src/telemetry/board/gigabyte/board_gigabyte_siv.cpp
    src/telemetry/board/msi/board_msi_center.cpp
    src/layout_model/layout_edit_active_region.cpp
    src/layout_model/layout_edit_helpers.cpp
    src/layout_model/layout_edit_hit_priority.cpp
    src/layout_model/layout_edit_layout_target.cpp
    src/layout_model/layout_edit_parameter_metadata.cpp
    src/layout_model/layout_edit_service.cpp
    src/layout_edit/layout_edit_tree.cpp
    src/layout_edit/layout_edit_parameter_edit.cpp
    src/layout_edit/layout_edit_target_descriptor.cpp
    src/layout_edit_dialog/theme_preview.cpp
    src/layout_edit/layout_edit_service.cpp
    src/layout_edit/layout_edit_controller.cpp
    src/layout_edit/layout_edit_hit_test.cpp
    src/layout_edit/layout_edit_tooltip.cpp
    src/layout_edit/layout_edit_tooltip_payload.cpp
    src/layout_edit/layout_edit_tooltip_text.cpp
    src/layout_edit/layout_edit_trace_session.cpp
    src/layout_edit/impl/layout_snap_solver.cpp
    src/util/numeric_safety.cpp
    src/util/file_path.cpp
    src/telemetry/metrics.cpp
    src/layout_model/dashboard_overlay_state.cpp
    src/widget/card_chrome_layout.cpp
    src/renderer/renderer.cpp
    src/renderer/png_export.cpp
    src/renderer/layout_guide_sheet_palette.cpp
    src/renderer/render_types.cpp
    src/renderer/impl/d2d_renderer.cpp
    src/renderer/impl/d2d_cache.cpp
    src/renderer/impl/d2d_render_conversions.cpp
    src/layout_model/layout_edit_anchor_shape.cpp
    src/dashboard_renderer/impl/metric_lookup_cache.cpp
    src/dashboard_renderer/impl/animation_timeline.cpp
    src/dashboard_renderer/impl/layout_edit_overlay_renderer.cpp
    src/dashboard_renderer/impl/render_thread.cpp
    src/renderer/impl/palette.cpp
    src/renderer/impl/text_width_cache.cpp
    src/widget/animation.cpp
    src/widget/animation_types.cpp
    src/widget/widget.cpp
    src/widget/widget_host.cpp
    src/widget/layout_edit_types.cpp
    src/widget/impl/animation_primitives.cpp
    src/widget/impl/card_chrome.cpp
    src/widget/impl/pill_bar.cpp
    src/widget/impl/text.cpp
    src/widget/impl/gauge.cpp
    src/widget/impl/metric_list.cpp
    src/widget/impl/throughput.cpp
    src/widget/impl/network_footer.cpp
    src/widget/impl/vertical_spacer.cpp
    src/widget/impl/vertical_spring.cpp
    src/widget/impl/drive_usage_list.cpp
    src/widget/impl/clock_time.cpp
    src/widget/impl/clock_date.cpp
    src/dashboard_renderer/impl/layout_resolver.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_placement.cpp
    src/layout_guide_sheet/layout_guide_sheet.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_planner.cpp
    src/layout_guide_sheet/impl/layout_guide_sheet_renderer.cpp
    src/dashboard_renderer/impl/dashboard_renderer_benchmark.cpp
    src/dashboard_renderer/layout_guide_sheet_support.cpp
    src/dashboard_renderer/dashboard_renderer.cpp
    src/util/localization_catalog.cpp
    src/util/resource_loader.cpp
    src/util/resource_strings.cpp
    src/util/lightweight_mutex.cpp
    src/util/high_precision_timer.cpp
    src/util/text_encoding.cpp
    src/util/trace.cpp
    src/util/trace_timing.cpp
    src/vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    src/vendor/adlx/SDK/Platform/Windows/WinAPIs.cpp
)
target_sources(CaseDashBenchmarks PRIVATE
    $<TARGET_OBJECTS:CaseDashGigabyte>
    $<TARGET_OBJECTS:CaseDashMsi>
)

get_target_property(casedash_release_inline_limited_sources CaseDash SOURCES)
get_target_property(casedash_headless_sources CaseDashHeadless SOURCES)
get_target_property(casedash_benchmarks_sources CaseDashBenchmarks SOURCES)
list(APPEND casedash_release_inline_limited_sources ${casedash_headless_sources})
list(APPEND casedash_release_inline_limited_sources ${casedash_benchmarks_sources})
list(FILTER casedash_release_inline_limited_sources INCLUDE REGEX "\\.cpp$")
list(REMOVE_DUPLICATES casedash_release_inline_limited_sources)
list(REMOVE_ITEM casedash_release_inline_limited_sources ${casedash_release_noinline_sources})
# Size: /Ob1 measured smaller for normal Release sources while the cold-path list keeps /Ob0.
set_property(SOURCE ${casedash_release_inline_limited_sources} APPEND PROPERTY
    COMPILE_OPTIONS "$<$<CONFIG:Release>:/Ob1>"
)

target_include_directories(CaseDashBenchmarks PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
    "${CMAKE_SOURCE_DIR}/src/vendor/adlx"
    "${CASEDASH_GENERATED_DIR}"
)
add_dependencies(CaseDashBenchmarks CaseDashGeneratedResources CaseDashGeneratedConfigMeta)
target_compile_definitions(CaseDashBenchmarks PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _USE_STD_VECTOR_ALGORITHMS=0
    CASEDASH_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CASEDASH_ENABLE_LAYOUT_GUIDE_SHEET_OUTPUT=1
)
target_compile_options(CaseDashBenchmarks PRIVATE
    /W4
    /wd4530
    /permissive-
    /GR-
    /w14505
    /we4505
    /Zi
    $<$<CONFIG:Release>:/Os>
    $<$<CONFIG:Release>:/GL>
)
target_link_options(CaseDashBenchmarks PRIVATE
    $<$<CONFIG:Debug>:/DEBUG>
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/DEBUG:FULL>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/LTCG>
    $<$<AND:$<CONFIG:Release>,$<BOOL:${CASEDASH_LINK_MAPS}>>:/MAP:$<TARGET_FILE_DIR:CaseDashBenchmarks>/CaseDashBenchmarks.map>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL>
    /MANIFEST:NO
)
target_link_libraries(CaseDashBenchmarks PRIVATE
    user32
    gdi32
    d3d11
    d2d1
    dwrite
    pdh
    iphlpapi
    dxgi
    ws2_32
    ole32
    advapi32
    ${MSCOREE_LIB}
)
set_target_properties(CaseDashBenchmarks PROPERTIES
    PDB_NAME_RELEASE "CaseDashBenchmarks"
    PDB_OUTPUT_DIRECTORY_RELEASE "${output_root}"
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)



