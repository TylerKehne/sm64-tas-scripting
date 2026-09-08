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

# Download dependencies
include(FetchContent)

# CMake 4.x refuses projects whose cmake_minimum_required is below 3.5, and nlohmann/json
# 3.11.2 declares 3.1. This tells CMake >= 3.31 to treat such dependencies as 3.5; older
# CMake ignores the variable. Remove once the dependencies are bumped.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(json 
	DOWNLOAD_EXTRACT_TIMESTAMP ON
	URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
	URL_HASH SHA256=8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f
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

# Warnings as errors on every first-party target (ROADMAP 1.5). On in CI; off by default
# locally (cmake/WarningsAsErrors.cmake). Locally: scripts\build.ps1 -CMakeArgs '-DTASFW_WARNINGS_AS_ERRORS=ON'.
option(TASFW_WARNINGS_AS_ERRORS "Treat warnings as errors in every first-party target" OFF)
if(TASFW_WARNINGS_AS_ERRORS)
	include(WarningsAsErrors)
	tasfw_warnings_as_errors()
endif()