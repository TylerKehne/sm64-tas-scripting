set(CMAKE_EXPORT_COMPILE_COMMANDS true)

cmake_minimum_required(VERSION 3.22.1 FATAL_ERROR)
project(sm64-tas-scripting
	DESCRIPTION "TASing framework for SM64"
	LANGUAGES CXX
)

# Check build type
# https://www.kitware.com/cmake-and-the-default-build-type/
set(SM64_TASFW_DEFAULT_BUILD "Release")

if(EXISTS "${PROJECT_SOURCE_DIR}/.git")
	set(SM64_TASFW_DEFAULT_BUILD "Debug")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to '${SM64_TASFW_DEFAULT_BUILD}' as none was specified.")
	set(CMAKE_BUILD_TYPE "${SM64_TASFW_DEFAULT_BUILD}" CACHE
		STRING "Choose the type of build." FORCE)

	# Set the possible values of build type for cmake-gui
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
		"Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
# add CMake modules in /cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AddOptimizationFlags)
include(SystemIncludes)

# Dependencies are fetched by CMake. Every tarball is pinned by hash and cached in
# TASFW_DOWNLOAD_DIR (under build/, gitignored, shared by every build directory), so a build
# with the cache is offline, and a version bump always downloads because the cached file no
# longer matches its hash. Do not point FETCHCONTENT_SOURCE_DIR_* at another build tree's
# <name>-src: that reuse is version-blind and once kept an old json here after a bump.
set(TASFW_DOWNLOAD_DIR "${PROJECT_SOURCE_DIR}/build/downloads" CACHE PATH "Where FetchContent caches dependency tarballs")
include(FetchContent)

# CMake 4.x refuses projects whose cmake_minimum_required is below 3.5; doctest 2.4.11
# declares 3.0 (nlohmann/json did too until 3.12). This tells CMake >= 3.31 to treat such
# dependencies as 3.5; older CMake ignores the variable. Remove once doctest is bumped.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(json
	DOWNLOAD_EXTRACT_TIMESTAMP ON
	DOWNLOAD_DIR "${TASFW_DOWNLOAD_DIR}"
	DOWNLOAD_NAME json-3.12.0.tar.xz
	URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
	URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
FetchContent_MakeAvailable(json)
tasfw_system_includes(nlohmann_json)

# Library components
add_subdirectory(tasfw-core)
add_subdirectory(tasfw-scripts)
add_subdirectory(tasfw-scattershot)
add_subdirectory(tasfw-bruteforcers)
add_subdirectory(tasfw-resources)

add_subdirectory(tasfw-tools)
add_subdirectory(tasfw-testing)

# Correctness tests (ROADMAP 1.2). Run with scripts/test.ps1 or ctest.
option(TASFW_BUILD_TESTS "Build the tasfw-tests correctness suite" ON)
if(TASFW_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tasfw-tests)
endif()

# Performance suite (docs/performance.md). Run with scripts/perf.ps1.
option(TASFW_BUILD_PERF "Build the tasfw-perf benchmark suite" ON)
if(TASFW_BUILD_PERF)
	add_subdirectory(tasfw-perf)
endif()

# Warning level (/W3, -Wall -Wextra; ROADMAP 1.7) on every first-party target, and warnings
# as errors (ROADMAP 1.5): on in CI, off by default locally (cmake/Warnings.cmake). Locally:
# scripts\build.ps1 -CMakeArgs '-DTASFW_WARNINGS_AS_ERRORS=ON'.
include(Warnings)
tasfw_warning_level()
option(TASFW_WARNINGS_AS_ERRORS "Treat warnings as errors in every first-party target" OFF)
if(TASFW_WARNINGS_AS_ERRORS)
	tasfw_warnings_as_errors()
endif()