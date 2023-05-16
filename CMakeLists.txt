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

# Download dependencies
include(FetchContent)

FetchContent_Declare(json 
	DOWNLOAD_EXTRACT_TIMESTAMP ON
	URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
	URL_HASH SHA256=8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f
)
FetchContent_MakeAvailable(json)


# Library components
add_subdirectory(tasfw-core)
add_subdirectory(tasfw-scripts)
add_subdirectory(tasfw-scattershot)
add_subdirectory(tasfw-bruteforcers)
add_subdirectory(tasfw-resources)