function(casedash_read_base_version out_var)
    file(READ "${CMAKE_SOURCE_DIR}/VERSION" casedash_base_version)
    string(STRIP "${casedash_base_version}" casedash_base_version)
    if(NOT casedash_base_version MATCHES "^[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
        message(FATAL_ERROR "VERSION must use major.minor or major.minor.patch format.")
    endif()
    set(${out_var} "${casedash_base_version}" PARENT_SCOPE)
endfunction()

function(casedash_configure_version_metadata)
    casedash_read_base_version(CASEDASH_BASE_VERSION)

    string(REPLACE "." ";" casedash_version_parts "${CASEDASH_BASE_VERSION}")
    list(GET casedash_version_parts 0 CASEDASH_VERSION_MAJOR)
    list(GET casedash_version_parts 1 CASEDASH_VERSION_MINOR)
    list(LENGTH casedash_version_parts casedash_version_part_count)
    if(casedash_version_part_count GREATER 2)
        list(GET casedash_version_parts 2 CASEDASH_VERSION_PATCH)
    else()
        set(CASEDASH_VERSION_PATCH 0)
    endif()
    set(CASEDASH_VERSION_BUILD 0)
    set(CASEDASH_WIN32_VERSION_COMMA
        "${CASEDASH_VERSION_MAJOR},${CASEDASH_VERSION_MINOR},${CASEDASH_VERSION_PATCH},${CASEDASH_VERSION_BUILD}"
    )
    set(CASEDASH_WIN32_VERSION_DOT
        "${CASEDASH_VERSION_MAJOR}.${CASEDASH_VERSION_MINOR}.${CASEDASH_VERSION_PATCH}.${CASEDASH_VERSION_BUILD}"
    )

    set(CASEDASH_GIT_COMMIT "unknown")
    set(CASEDASH_GIT_COMMIT_SHORT "unknown")
    set(CASEDASH_GIT_TAG "")
    set(CASEDASH_GIT_DIRTY TRUE)

    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE CASEDASH_GIT_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 --verify HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE CASEDASH_GIT_COMMIT_SHORT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE CASEDASH_GIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" diff-index --quiet HEAD --
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE casedash_git_dirty_result
            ERROR_QUIET
        )
        if(casedash_git_dirty_result EQUAL 0)
            set(CASEDASH_GIT_DIRTY FALSE)
        else()
            set(CASEDASH_GIT_DIRTY TRUE)
        endif()
    endif()

    set(CASEDASH_RELEASE_TAG "v${CASEDASH_BASE_VERSION}")
    if(CASEDASH_GIT_TAG STREQUAL CASEDASH_RELEASE_TAG AND NOT CASEDASH_GIT_DIRTY)
        set(CASEDASH_OFFICIAL_RELEASE TRUE)
        set(CASEDASH_VERSION "${CASEDASH_BASE_VERSION}")
        set(CASEDASH_BUILD_KIND "official")
        set(CASEDASH_WIN32_FILEFLAGS "0x0L")
        set(CASEDASH_SPECIAL_BUILD "")
    else()
        set(CASEDASH_OFFICIAL_RELEASE FALSE)
        set(CASEDASH_VERSION "${CASEDASH_BASE_VERSION}-dev+g${CASEDASH_GIT_COMMIT_SHORT}")
        if(CASEDASH_GIT_DIRTY)
            string(APPEND CASEDASH_VERSION ".dirty")
        endif()
        set(CASEDASH_BUILD_KIND "development")
        set(CASEDASH_WIN32_FILEFLAGS "VS_FF_PRERELEASE")
        set(CASEDASH_SPECIAL_BUILD "${CASEDASH_BUILD_KIND}")
    endif()

    if(CASEDASH_OFFICIAL_RELEASE)
        set(CASEDASH_OFFICIAL_RELEASE_CPP "true")
    else()
        set(CASEDASH_OFFICIAL_RELEASE_CPP "false")
    endif()
    if(CASEDASH_GIT_DIRTY)
        set(CASEDASH_GIT_DIRTY_CPP "true")
    else()
        set(CASEDASH_GIT_DIRTY_CPP "false")
    endif()

    set(CASEDASH_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${CASEDASH_GENERATED_DIR}")
    set(casedash_manifest_path "${CASEDASH_GENERATED_DIR}/CaseDash.manifest")
    set(casedash_headless_manifest_path "${CASEDASH_GENERATED_DIR}/CaseDashHeadless.manifest")

    set(CASEDASH_MANIFEST_NAME "CaseDash")
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/CaseDash.manifest.in"
        "${casedash_manifest_path}"
        @ONLY
    )
    set(CASEDASH_MANIFEST_NAME "CaseDashHeadless")
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/CaseDash.manifest.in"
        "${casedash_headless_manifest_path}"
        @ONLY
    )
    configure_file(
        "${CMAKE_SOURCE_DIR}/src/main/build_version.h.in"
        "${CASEDASH_GENERATED_DIR}/build_version.h"
        @ONLY
    )

    set(CASEDASH_FILE_DESCRIPTION "CaseDash")
    set(CASEDASH_ORIGINAL_FILENAME "CaseDash.exe")
    set(CASEDASH_MANIFEST_PATH "${casedash_manifest_path}")
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/CaseDashVersion.rc.in"
        "${CASEDASH_GENERATED_DIR}/CaseDashVersion.rc"
        @ONLY
    )

    set(CASEDASH_FILE_DESCRIPTION "CaseDash Headless")
    set(CASEDASH_ORIGINAL_FILENAME "CaseDashHeadless.exe")
    set(CASEDASH_MANIFEST_PATH "${casedash_headless_manifest_path}")
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/CaseDashVersion.rc.in"
        "${CASEDASH_GENERATED_DIR}/CaseDashHeadlessVersion.rc"
        @ONLY
    )

    set(CASEDASH_FILE_DESCRIPTION "CaseDash Benchmarks")
    set(CASEDASH_ORIGINAL_FILENAME "CaseDashBenchmarks.exe")
    set(CASEDASH_MANIFEST_PATH "${casedash_manifest_path}")
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/CaseDashVersion.rc.in"
        "${CASEDASH_GENERATED_DIR}/CaseDashBenchmarksVersion.rc"
        @ONLY
    )

    set(CASEDASH_GENERATED_DIR "${CASEDASH_GENERATED_DIR}" PARENT_SCOPE)
    set(CASEDASH_VERSION_RC "${CMAKE_BINARY_DIR}/generated/CaseDashVersion.rc" PARENT_SCOPE)
    set(CASEDASH_HEADLESS_VERSION_RC "${CMAKE_BINARY_DIR}/generated/CaseDashHeadlessVersion.rc" PARENT_SCOPE)
    set(CASEDASH_BENCHMARKS_VERSION_RC "${CMAKE_BINARY_DIR}/generated/CaseDashBenchmarksVersion.rc" PARENT_SCOPE)
    set(CASEDASH_MANIFEST_PATH "${casedash_manifest_path}" PARENT_SCOPE)
    set(CASEDASH_HEADLESS_MANIFEST_PATH "${casedash_headless_manifest_path}" PARENT_SCOPE)
    set(CASEDASH_VERSION "${CASEDASH_VERSION}" PARENT_SCOPE)
    set(CASEDASH_OFFICIAL_RELEASE "${CASEDASH_OFFICIAL_RELEASE}" PARENT_SCOPE)
endfunction()
