vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO tree-sitter/tree-sitter-cpp
    REF "v${VERSION}"
    SHA512 baebacf06ea1527132c641b4e2a2e997c501a63708d7afdb5d9456de519dbd652f25aee03a7b4112ef9a683fa176aaaf96d272de286223773a5d6cdf01605a2e
    HEAD_REF master
    PATCHES
        casedash-grammar.patch
)

set(CASEDASH_FORMAT_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../../../tools/format_config.json")
if(NOT EXISTS "${CASEDASH_FORMAT_CONFIG}")
    message(FATAL_ERROR "Missing native formatter macro config: ${CASEDASH_FORMAT_CONFIG}")
endif()

cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST CASEDASH_HOST_PATHS NORMALIZE)
set(CASEDASH_NODE_SEARCH_PATHS
    ${CASEDASH_HOST_PATHS}
    "$ENV{ProgramFiles}/nodejs"
    "C:/Tools/nodejs"
    "D:/Tools/nodejs"
)
if(DEFINED ENV{CASEDASH_NODE_EXE})
    file(TO_CMAKE_PATH "$ENV{CASEDASH_NODE_EXE}" NODE_EXE)
endif()
if(NOT NODE_EXE OR NOT EXISTS "${NODE_EXE}")
    find_program(NODE_EXE NAMES node PATHS ${CASEDASH_NODE_SEARCH_PATHS} NO_DEFAULT_PATH REQUIRED)
endif()
if(DEFINED ENV{CASEDASH_NPM_EXE})
    file(TO_CMAKE_PATH "$ENV{CASEDASH_NPM_EXE}" NPM_EXE)
endif()
if(NOT NPM_EXE OR NOT EXISTS "${NPM_EXE}")
    find_program(NPM_EXE NAMES npm.cmd npm PATHS ${CASEDASH_NODE_SEARCH_PATHS} NO_DEFAULT_PATH REQUIRED)
endif()

vcpkg_execute_required_process(
    COMMAND "${NPM_EXE}" install --ignore-scripts --production
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "tree-sitter-cpp-npm-install-${TARGET_TRIPLET}"
)
vcpkg_execute_required_process(
    COMMAND "${NODE_EXE}" "${CMAKE_CURRENT_LIST_DIR}/generate_case_dash_macro_config.js" "${CASEDASH_FORMAT_CONFIG}" "${SOURCE_PATH}/case_dash_macro_config.js"
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "tree-sitter-cpp-macro-config-${TARGET_TRIPLET}"
)

if(VCPKG_HOST_IS_WINDOWS)
    vcpkg_download_distfile(TREE_SITTER_CLI_ARCHIVE
        URLS "https://github.com/tree-sitter/tree-sitter/releases/download/v0.24.7/tree-sitter-windows-x64.gz"
        FILENAME "tree-sitter-cli-v0.24.7-windows-x64.gz"
        SHA512 4CEFF1C79CF8491B1099CBC401AC4F2B85BAC45716C8C4B24C3EDA35A38C01E4996000CAF86979323E3F6352B2BF61CE2904C971627AFC4B0BCDEFD4E40C8A36
    )
    set(TREE_SITTER_CLI "${CURRENT_BUILDTREES_DIR}/tree-sitter-cli/tree-sitter.exe")
    get_filename_component(CASEDASH_NODE_DIR "${NODE_EXE}" DIRECTORY)
    file(TO_NATIVE_PATH "${CASEDASH_NODE_DIR}" CASEDASH_NODE_DIR_NATIVE)
    file(TO_NATIVE_PATH "${TREE_SITTER_CLI}" TREE_SITTER_CLI_NATIVE)
    set(TREE_SITTER_GENERATE_COMMAND "${CURRENT_BUILDTREES_DIR}/tree-sitter-cpp-generate.cmd")
    file(WRITE "${TREE_SITTER_GENERATE_COMMAND}" "@echo off\r\nset \"PATH=${CASEDASH_NODE_DIR_NATIVE};%PATH%\"\r\n\"${TREE_SITTER_CLI_NATIVE}\" generate\r\n")
else()
    message(FATAL_ERROR "The CaseDash tree-sitter-cpp overlay currently supports Windows hosts only")
endif()
vcpkg_execute_required_process(
    COMMAND "${NODE_EXE}" "${CMAKE_CURRENT_LIST_DIR}/unpack_tree_sitter_cli.js" "${TREE_SITTER_CLI_ARCHIVE}" "${TREE_SITTER_CLI}"
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "tree-sitter-cpp-unpack-cli-${TARGET_TRIPLET}"
)
vcpkg_execute_required_process(
    COMMAND "${TREE_SITTER_GENERATE_COMMAND}"
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "tree-sitter-cpp-generate-${TARGET_TRIPLET}"
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME "unofficial-tree-sitter-cpp")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
