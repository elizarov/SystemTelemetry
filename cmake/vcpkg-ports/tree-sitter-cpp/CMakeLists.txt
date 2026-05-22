cmake_minimum_required(VERSION 3.20)

project(tree-sitter-cpp LANGUAGES C)

include(GNUInstallDirs)

set(generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${generated_include_dir}")
file(WRITE "${generated_include_dir}/tree_sitter_cpp.h" [=[
#pragma once

#include <tree_sitter/api.h>

#ifdef __cplusplus
extern "C" {
#endif

const TSLanguage* tree_sitter_cpp(void);

#ifdef __cplusplus
}
#endif
]=])

add_library(tree-sitter-cpp STATIC
    src/parser.c
    src/scanner.c
)
target_include_directories(tree-sitter-cpp
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PUBLIC
        "$<BUILD_INTERFACE:${generated_include_dir}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

install(FILES "${generated_include_dir}/tree_sitter_cpp.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(TARGETS tree-sitter-cpp
    EXPORT unofficial-tree-sitter-cpp-config
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(EXPORT unofficial-tree-sitter-cpp-config
    NAMESPACE unofficial::tree-sitter-cpp::
    DESTINATION share/unofficial-tree-sitter-cpp
)
