#===============================================================================
# BMP581 driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   bmp581, consumed as bmp581::bmp581 both through add_subdirectory() and
#   through find_package(bmp581) after install.
#
#   cmake -S . -B build -DBMP581_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/bmp581_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${BMP581_SOURCE_FILES}
        INCLUDE_DIRS ${BMP581_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${BMP581_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(BMP581_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(bmp581_driver
    VERSION ${BMP581_VERSION}
    DESCRIPTION "C++20 driver for the Bosch BMP581 barometric pressure and temperature sensor on I2C and SPI."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because bmp581.hpp includes
# "../src/bmp581.ipp" relative to itself:
#   <prefix>/include/bmp581/inc/*.hpp, bmp581_version.h
#   <prefix>/include/bmp581/src/bmp581.ipp
set(BMP581_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/bmp581/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${BMP581_TARGET_NAME} INTERFACE)
target_include_directories(${BMP581_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${BMP581_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${BMP581_INSTALL_INCLUDEDIR}>
)
target_compile_features(${BMP581_TARGET_NAME} INTERFACE cxx_std_20)

# bmp581::bmp581 everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(bmp581::bmp581 ALIAS ${BMP581_TARGET_NAME})
set_target_properties(${BMP581_TARGET_NAME} PROPERTIES EXPORT_NAME bmp581)

option(BMP581_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links bmp581::bmp581" OFF)
if(BMP581_ENABLE_WARNINGS)
    target_compile_options(${BMP581_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(BMP581_BUILD_TESTS "Build the host tests in tests/" OFF)
option(BMP581_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(BMP581_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${BMP581_TARGET_NAME} EXPORT bmp581Targets
    INCLUDES DESTINATION ${BMP581_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${BMP581_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${BMP581_VERSION_HEADER}" DESTINATION ${BMP581_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bmp581/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT bmp581Targets
    FILE bmp581Targets.cmake
    NAMESPACE bmp581::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bmp581)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/bmp581Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bmp581Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bmp581)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/bmp581ConfigVersion.cmake"
    VERSION ${BMP581_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/bmp581Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/bmp581ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bmp581)
