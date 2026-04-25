cmake_minimum_required(VERSION 3.26)

project(SystemTelemetry LANGUAGES CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "SystemTelemetry currently supports MSVC builds only.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

foreach(cxx_flags_var IN ITEMS
    CMAKE_CXX_FLAGS
    CMAKE_CXX_FLAGS_DEBUG
    CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS_MINSIZEREL
)
    string(REPLACE "/EHsc" "" ${cxx_flags_var} "${${cxx_flags_var}}")
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
add_executable(SystemTelemetry WIN32
    resources/SystemTelemetry.rc
    src/main/main.cpp
    src/diagnostics/diagnostics.cpp
    src/main/autostart.cpp
    src/util/command_line.cpp
    src/main/config_io.cpp
    src/display/display_config.cpp
    src/diagnostics/diagnostics_options.cpp
    src/display/monitor.cpp
    src/util/paths.cpp
    src/util/strings.cpp
    src/dashboard/dashboard_controller.cpp
    src/dashboard/dashboard_app.cpp
    src/dashboard/dashboard_shell_ui.cpp
    src/layout_edit_dialog/layout_edit_dialog.cpp
    src/layout_edit_dialog/impl/dialog_proc.cpp
    src/layout_edit_dialog/impl/editors.cpp
    src/layout_edit_dialog/impl/pane.cpp
    src/layout_edit_dialog/impl/state.cpp
    src/layout_edit_dialog/impl/trace.cpp
    src/layout_edit_dialog/impl/tree.cpp
    src/layout_edit_dialog/impl/util.cpp
    src/layout_edit/layout_edit_controller.cpp
    src/layout_edit/layout_edit_trace_session.cpp
    src/layout_edit/layout_edit_helpers.cpp
    src/layout_edit/layout_edit_hit_priority.cpp
    src/layout_edit/layout_edit_tree.cpp
    src/layout_edit/layout_edit_parameter_metadata.cpp
    src/layout_edit/layout_edit_tooltip.cpp
    src/layout_edit/layout_edit_tooltip_text.cpp
    src/layout_edit/layout_edit_service.cpp
    src/config/config.cpp
    src/config/metric_catalog.cpp
    src/config/config_parser.cpp
    src/config/config_writer.cpp
    src/config/config_resolution.cpp
    src/util/localization_catalog.cpp
    src/util/resource_loader.cpp
    src/layout_edit/impl/layout_snap_solver.cpp
    src/util/numeric_safety.cpp
    src/telemetry/impl/system_info_support.cpp
    src/telemetry/telemetry.cpp
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
    src/telemetry/gpu/gpu_vendor.cpp
    src/telemetry/gpu/amd/gpu_amd_adl.cpp
    src/telemetry/board/board_vendor.cpp
    src/telemetry/metrics.cpp
    src/dashboard/dashboard_overlay_state.cpp
    src/widget/card_chrome_layout.cpp
    src/widget/render_types.cpp
    src/dashboard_renderer/impl/d2d_cache.cpp
    src/dashboard_renderer/impl/d2d_render_conversions.cpp
    src/dashboard_renderer/impl/palette.cpp
    src/dashboard_renderer/impl/text_width_cache.cpp
    src/widget/widget.cpp
    src/widget/impl/card_chrome.cpp
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
    src/util/utf8.cpp
    src/util/trace.cpp
    src/vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    src/vendor/adlx/SDK/Platform/Windows/WinAPIs.cpp
)

set_source_files_properties(resources/SystemTelemetry.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/config.ini;${CMAKE_SOURCE_DIR}/resources/localization.ini"
)

add_library(SystemTelemetryGigabyte OBJECT
    src/telemetry/board/gigabyte/board_gigabyte_siv.cpp
)

set_target_properties(SystemTelemetryGigabyte PROPERTIES
    COMMON_LANGUAGE_RUNTIME ""
    VS_DOTNET_TARGET_FRAMEWORK_VERSION "v4.0"
)

target_include_directories(SystemTelemetryGigabyte PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
)

target_compile_definitions(SystemTelemetryGigabyte PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_options(SystemTelemetryGigabyte PRIVATE
    /clr
    /W4
    /permissive-
    /Zi
)

target_sources(SystemTelemetry PRIVATE
    $<TARGET_OBJECTS:SystemTelemetryGigabyte>
)

target_include_directories(SystemTelemetry PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
    "${CMAKE_SOURCE_DIR}/src/vendor/adlx"
)

target_compile_definitions(SystemTelemetry PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_options(SystemTelemetry PRIVATE
    /EHsc
    /W4
    /permissive-
    /Zi
)

target_link_options(SystemTelemetry PRIVATE
    $<$<CONFIG:Debug>:/DEBUG>
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL>
    /MANIFEST:NO
)

target_link_libraries(SystemTelemetry PRIVATE
    user32
    gdi32
    d2d1
    dwrite
    windowscodecs
    pdh
    iphlpapi
    dxgi
    comctl32
    ws2_32
    ole32
    oleaut32
    advapi32
    wtsapi32
    ${MSCOREE_LIB}
)

set_target_properties(SystemTelemetry PROPERTIES
    OUTPUT_NAME "SystemTelemetry"
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)

enable_testing()

set(systemtelemetry_vcpkg_triplet "x64-windows")
if(DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "")
    set(systemtelemetry_vcpkg_triplet "${VCPKG_TARGET_TRIPLET}")
endif()

set(systemtelemetry_vcpkg_installed_dir "")
if(DEFINED VCPKG_INSTALLED_DIR AND NOT VCPKG_INSTALLED_DIR STREQUAL "")
    set(systemtelemetry_vcpkg_installed_dir "${VCPKG_INSTALLED_DIR}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/${systemtelemetry_vcpkg_triplet}/share/gtest/GTestConfig.cmake")
    set(systemtelemetry_vcpkg_installed_dir "${CMAKE_SOURCE_DIR}/vcpkg")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/build/vcpkg_installed/${systemtelemetry_vcpkg_triplet}/share/gtest/GTestConfig.cmake")
    set(systemtelemetry_vcpkg_installed_dir "${CMAKE_SOURCE_DIR}/build/vcpkg_installed")
endif()

if(NOT systemtelemetry_vcpkg_installed_dir STREQUAL "" AND
   EXISTS "${systemtelemetry_vcpkg_installed_dir}/${systemtelemetry_vcpkg_triplet}/share/gtest/GTestConfig.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${systemtelemetry_vcpkg_installed_dir}/${systemtelemetry_vcpkg_triplet}")
endif()

find_package(GTest CONFIG REQUIRED)

add_executable(SystemTelemetryTests
    tests/enum_string_tests.cpp
    tests/metrics_tests.cpp
    tests/snapshot_dump_tests.cpp
    tests/widget_factory_tests.cpp
    tests/widget_class_tests.cpp
    tests/metric_list_widget_tests.cpp
    tests/card_chrome_layout_tests.cpp
    tests/layout_snap_solver_tests.cpp
    tests/layout_edit_parameter_apply_tests.cpp
    tests/layout_edit_types_tests.cpp
    tests/layout_edit_tree_tests.cpp
    tests/layout_edit_tooltip_tests.cpp
    tests/localization_catalog_tests.cpp
    tests/config_parser_tests.cpp
    tests/config_resolution_tests.cpp
    tests/config_writer_tests.cpp
    tests/system_info_support_tests.cpp
    tests/telemetry_retained_history_tests.cpp
    src/config/config.cpp
    src/config/metric_catalog.cpp
    src/config/config_parser.cpp
    src/config/config_writer.cpp
    src/config/config_resolution.cpp
    src/layout_edit/layout_edit_helpers.cpp
    src/layout_edit/layout_edit_hit_priority.cpp
    src/layout_edit/layout_edit_tree.cpp
    src/layout_edit/layout_edit_parameter_metadata.cpp
    src/layout_edit/layout_edit_tooltip.cpp
    src/util/strings.cpp
    src/util/localization_catalog.cpp
    src/util/resource_loader.cpp
    src/layout_edit/impl/layout_snap_solver.cpp
    src/util/numeric_safety.cpp
    src/telemetry/impl/system_info_support.cpp
    src/telemetry/metrics.cpp
    src/widget/card_chrome_layout.cpp
    src/widget/widget.cpp
    src/widget/impl/card_chrome.cpp
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
    src/widget/render_types.cpp
    src/diagnostics/snapshot_dump.cpp
    src/telemetry/impl/retained_history.cpp
    src/util/utf8.cpp
  )
target_include_directories(SystemTelemetryTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
)
target_compile_definitions(SystemTelemetryTests PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SYSTEMTELEMETRY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(SystemTelemetryTests PRIVATE
    /EHsc
    /W4
    /permissive-
    /Zi
)
target_link_options(SystemTelemetryTests PRIVATE
    $<$<CONFIG:Debug>:/DEBUG>
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL>
)
target_link_libraries(SystemTelemetryTests PRIVATE
    GTest::gtest_main
)
set_target_properties(SystemTelemetryTests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)
add_custom_command(TARGET SystemTelemetryTests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:SystemTelemetryTests>
        $<TARGET_FILE_DIR:SystemTelemetryTests>
    COMMAND_EXPAND_LISTS
)

add_test(NAME SystemTelemetryTests COMMAND SystemTelemetryTests)

add_executable(SystemTelemetryBenchmarks
    resources/SystemTelemetry.rc
    tests/benchmarks.cpp
    src/util/strings.cpp
    src/config/config.cpp
    src/config/metric_catalog.cpp
    src/config/config_parser.cpp
    src/config/config_resolution.cpp
    src/telemetry/impl/system_info_support.cpp
    src/telemetry/telemetry.cpp
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
    src/telemetry/gpu/gpu_vendor.cpp
    src/telemetry/gpu/amd/gpu_amd_adl.cpp
    src/telemetry/board/board_vendor.cpp
    src/layout_edit/layout_edit_helpers.cpp
    src/layout_edit/layout_edit_hit_priority.cpp
    src/layout_edit/layout_edit_parameter_metadata.cpp
    src/layout_edit/layout_edit_tree.cpp
    src/layout_edit/layout_edit_controller.cpp
    src/layout_edit/layout_edit_trace_session.cpp
    src/layout_edit/layout_edit_service.cpp
    src/layout_edit/impl/layout_snap_solver.cpp
    src/util/numeric_safety.cpp
    src/telemetry/metrics.cpp
    src/dashboard/dashboard_overlay_state.cpp
    src/widget/card_chrome_layout.cpp
    src/widget/render_types.cpp
    src/dashboard_renderer/impl/d2d_cache.cpp
    src/dashboard_renderer/impl/d2d_render_conversions.cpp
    src/dashboard_renderer/impl/palette.cpp
    src/dashboard_renderer/impl/text_width_cache.cpp
    src/widget/widget.cpp
    src/widget/impl/card_chrome.cpp
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
    src/util/resource_loader.cpp
    src/util/utf8.cpp
    src/util/trace.cpp
    src/vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    src/vendor/adlx/SDK/Platform/Windows/WinAPIs.cpp
)
target_sources(SystemTelemetryBenchmarks PRIVATE
    $<TARGET_OBJECTS:SystemTelemetryGigabyte>
)
target_include_directories(SystemTelemetryBenchmarks PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
    "${CMAKE_SOURCE_DIR}/src/vendor/adlx"
)
target_compile_definitions(SystemTelemetryBenchmarks PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SYSTEMTELEMETRY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(SystemTelemetryBenchmarks PRIVATE
    /EHsc
    /W4
    /permissive-
    /Zi
)
target_link_options(SystemTelemetryBenchmarks PRIVATE
    $<$<CONFIG:Debug>:/DEBUG>
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/DEBUG:FULL>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL>
    /MANIFEST:NO
)
target_link_libraries(SystemTelemetryBenchmarks PRIVATE
    user32
    gdi32
    d2d1
    dwrite
    windowscodecs
    pdh
    iphlpapi
    dxgi
    ws2_32
    ole32
    oleaut32
    advapi32
    ${MSCOREE_LIB}
)
set_target_properties(SystemTelemetryBenchmarks PROPERTIES
    PDB_NAME_RELEASE "SystemTelemetryBenchmarks"
    PDB_OUTPUT_DIRECTORY_RELEASE "${output_root}"
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)

