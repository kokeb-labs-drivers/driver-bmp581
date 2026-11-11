#===============================================================================
# BMP581 driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(BMP581_TARGET_NAME "bmp581")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(BMP581_VERSION_MAJOR 0)
set(BMP581_VERSION_MINOR 1)
set(BMP581_VERSION_PATCH 0)
set(BMP581_VERSION "${BMP581_VERSION_MAJOR}.${BMP581_VERSION_MINOR}.${BMP581_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(BMP581_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/bmp581_version.h.in")
set(BMP581_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/bmp581_generated")
set(BMP581_VERSION_HEADER "${BMP581_VERSION_HEADER_DIR}/bmp581_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${BMP581_VERSION_HEADER_DIR}")
configure_file("${BMP581_VERSION_TEMPLATE}" "${BMP581_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(BMP581_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${BMP581_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/bmp581.ipp, included by inc/bmp581.hpp.
set(BMP581_SOURCE_FILES)

set(BMP581_IDF_REQUIRES)
