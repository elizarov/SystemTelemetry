cmake_minimum_required(VERSION 3.26)

project(SystemTelemetry LANGUAGES CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "SystemTelemetry currently supports MSVC builds only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

list(APPEND mscoree_search_paths
    "C:/Program Files (x86)/Windows Kits/8.1/Lib/winv6.3/um/x64"
)

find_library(MSCOREE_LIB
    NAMES mscoree
    PATHS ${mscoree_search_paths}
)

if(NOT MSCOREE_LIB)
    message(FATAL_ERROR "mscoree.lib was not found in the active Windows SDK. Run build.cmd from a configured developer prompt.")
endif()
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
    src/dashboard_metrics.cpp
    src/dashboard_renderer.cpp
    src/utf8.cpp
    src/trace.cpp
    src/vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    src/vendor/adlx/SDK/Platform/Windows/WinAPIs.cpp
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
)

target_compile_definitions(SystemTelemetryGigabyte PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_compile_options(SystemTelemetryGigabyte PRIVATE
    /EHa
    /W4
    /permissive-
    /Zi
)

target_sources(SystemTelemetry PRIVATE
    $<TARGET_OBJECTS:SystemTelemetryGigabyte>
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
    ${MSCOREE_LIB}
)

set_target_properties(SystemTelemetry PROPERTIES
    OUTPUT_NAME "SystemTelemetry"
    VS_DEBUGGER_WORKING_DIRECTORY "${output_root}"
)

