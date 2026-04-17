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
    src/app_main.cpp
    src/app_diagnostics.cpp
    src/app_autostart.cpp
    src/app_command_line.cpp
    src/app_config_io.cpp
    src/app_display_config.cpp
    src/diagnostics_options.cpp
    src/app_monitor.cpp
    src/app_paths.cpp
    src/app_strings.cpp
    src/dashboard_controller.cpp
    src/dashboard_app.cpp
    src/dashboard_shell_ui.cpp
    src/layout_edit_dialog.cpp
    src/layout_edit_dialog/dialog_proc.cpp
    src/layout_edit_dialog/editors.cpp
    src/layout_edit_dialog/pane.cpp
    src/layout_edit_dialog/state.cpp
    src/layout_edit_dialog/trace.cpp
    src/layout_edit_dialog/tree.cpp
    src/layout_edit_dialog/util.cpp
    src/layout_edit_controller.cpp
    src/layout_edit_trace_session.cpp
    src/layout_edit_types.cpp
    src/layout_edit_tree.cpp
    src/layout_edit_parameter.cpp
    src/layout_edit_tooltip.cpp
    src/layout_edit_service.cpp
    src/config.cpp
    src/config_parser.cpp
    src/config_writer.cpp
    src/config_resolution.cpp
    src/localization_catalog.cpp
    src/layout_snap_solver.cpp
    src/numeric_safety.cpp
    src/system_info_support.cpp
    src/telemetry.cpp
    src/telemetry/collector_state.cpp
    src/telemetry/collector_board.cpp
    src/telemetry/collector_cpu.cpp
    src/telemetry/collector_gpu.cpp
    src/telemetry/collector_support.cpp
    src/telemetry/retained_history.cpp
    src/telemetry/collector_storage.cpp
    src/telemetry/collector_network.cpp
    src/telemetry_runtime.cpp
    src/telemetry_runtime_fake.cpp
    src/telemetry_runtime_factory.cpp
    src/snapshot_dump.cpp
    src/gpu_vendor.cpp
    src/gpu_amd_adl.cpp
    src/board_vendor.cpp
    src/dashboard_metrics.cpp
    src/render_types.cpp
    src/widget.cpp
    src/widget/text.cpp
    src/widget/gauge.cpp
    src/widget/metric_list.cpp
    src/widget/throughput.cpp
    src/widget/network_footer.cpp
    src/widget/vertical_spacer.cpp
    src/widget/vertical_spring.cpp
    src/widget/drive_usage_list.cpp
    src/widget/clock_time.cpp
    src/widget/clock_date.cpp
    src/dashboard_layout_resolver.cpp
    src/dashboard_renderer.cpp
    src/utf8.cpp
    src/trace.cpp
    src/vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    src/vendor/adlx/SDK/Platform/Windows/WinAPIs.cpp
)

set_source_files_properties(resources/SystemTelemetry.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/config.ini;${CMAKE_SOURCE_DIR}/resources/localization.ini"
)

add_library(SystemTelemetryGigabyte OBJECT
    src/board_gigabyte_siv.cpp
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

if(EXISTS "${CMAKE_SOURCE_DIR}/build/vcpkg_installed/${systemtelemetry_vcpkg_triplet}/share/gtest/GTestConfig.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/build/vcpkg_installed/${systemtelemetry_vcpkg_triplet}")
endif()

find_package(GTest CONFIG REQUIRED)

add_executable(SystemTelemetryTests
    tests/enum_string_tests.cpp
    tests/dashboard_metrics_tests.cpp
    tests/snapshot_dump_tests.cpp
    tests/dashboard_widget_class_tests.cpp
    tests/layout_snap_solver_tests.cpp
    tests/layout_edit_parameter_apply_tests.cpp
    tests/layout_edit_types_tests.cpp
    tests/layout_edit_tree_tests.cpp
    tests/layout_edit_tooltip_tests.cpp
    tests/localization_catalog_tests.cpp
    tests/config_parser_tests.cpp
    tests/config_resolution_tests.cpp
    tests/config_writer_tests.cpp
    tests/telemetry_retained_history_tests.cpp
    src/config.cpp
    src/config_parser.cpp
    src/config_writer.cpp
    src/config_resolution.cpp
    src/layout_edit_types.cpp
    src/layout_edit_tree.cpp
    src/layout_edit_parameter.cpp
    src/layout_edit_tooltip.cpp
    src/localization_catalog.cpp
    src/layout_snap_solver.cpp
    src/numeric_safety.cpp
    src/dashboard_metrics.cpp
    src/render_types.cpp
    src/snapshot_dump.cpp
    src/telemetry/retained_history.cpp
    src/utf8.cpp
  )
target_include_directories(SystemTelemetryTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
)
target_compile_definitions(SystemTelemetryTests PRIVATE
    UNICODE
    _UNICODE
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
    tests/layout_drag_profile_benchmark.cpp
    src/config.cpp
    src/config_parser.cpp
    src/config_resolution.cpp
    src/layout_edit_types.cpp
    src/layout_edit_parameter.cpp
    src/layout_edit_controller.cpp
    src/layout_edit_trace_session.cpp
    src/layout_edit_service.cpp
    src/layout_snap_solver.cpp
    src/numeric_safety.cpp
    src/dashboard_metrics.cpp
    src/render_types.cpp
    src/widget.cpp
    src/widget/text.cpp
    src/widget/gauge.cpp
    src/widget/metric_list.cpp
    src/widget/throughput.cpp
    src/widget/network_footer.cpp
    src/widget/vertical_spacer.cpp
    src/widget/vertical_spring.cpp
    src/widget/drive_usage_list.cpp
    src/widget/clock_time.cpp
    src/widget/clock_date.cpp
    src/dashboard_layout_resolver.cpp
    src/dashboard_renderer.cpp
    src/utf8.cpp
    src/trace.cpp
)
target_include_directories(SystemTelemetryBenchmarks PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources"
)
target_compile_definitions(SystemTelemetryBenchmarks PRIVATE
    UNICODE
    _UNICODE
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
)
set_target_properties(SystemTelemetryBenchmarks PROPERTIES
    PDB_NAME_RELEASE "SystemTelemetryBenchmarks"
    PDB_OUTPUT_DIRECTORY_RELEASE "${output_root}"
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)

