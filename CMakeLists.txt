cmake_minimum_required(VERSION 3.26)

project(SystemTelemetry LANGUAGES CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "SystemTelemetry currently supports MSVC builds only.")
endif()

if(NOT DEFINED ENV{CSC})
    message(FATAL_ERROR "CSC is not set. Run build.cmd or configure CSC in devenv.cmd.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(output_root "${CMAKE_SOURCE_DIR}/build")
set(cmake_root "${output_root}/cmake")

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

add_executable(SystemTelemetry WIN32
    resources/SystemTelemetry.rc
    src/main.cpp
    src/config.cpp
    src/telemetry.cpp
    src/telemetry_runtime.cpp
    src/telemetry_runtime_fake.cpp
    src/snapshot_dump.cpp
    src/gpu_vendor.cpp
    src/gpu_amd_adl.cpp
    src/board_vendor.cpp
    src/board_gigabyte_siv.cpp
    src/dashboard_metrics.cpp
    src/dashboard_renderer.cpp
    src/utf8.cpp
    src/trace.cpp
    src/vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    src/vendor/adlx/SDK/Platform/Windows/WinAPIs.cpp
)

target_include_directories(SystemTelemetry PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
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
    /DEBUG
    /INCREMENTAL
    /MANIFEST:NO
)

target_link_libraries(SystemTelemetry PRIVATE
    user32
    gdi32
    gdiplus
    pdh
    iphlpapi
    dxgi
    comctl32
    ws2_32
    ole32
    oleaut32
    advapi32
)

set_target_properties(SystemTelemetry PROPERTIES
    OUTPUT_NAME "SystemTelemetry"
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)

set(probe_output "${output_root}/GigabyteSivProbe.exe")
set(probe_stamp "${cmake_root}/GigabyteSivProbe.stamp")
cmake_path(NATIVE_PATH probe_output probe_output_native)
cmake_path(NATIVE_PATH CMAKE_SOURCE_DIR source_root_native)
set(probe_source_native "${source_root_native}\\src\\GigabyteSivProbe.cs")

add_custom_command(
    OUTPUT "${probe_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_root}"
    COMMAND "$ENV{CSC}" /nologo /target:exe /platform:x64 "/out:${probe_output_native}" "${probe_source_native}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${probe_stamp}"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/GigabyteSivProbe.cs"
    COMMENT "Building GigabyteSivProbe.exe"
    VERBATIM
)

add_custom_target(GigabyteSivProbe ALL DEPENDS "${probe_stamp}")
