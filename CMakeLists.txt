set(CMAKE_EXPORT_COMPILE_COMMANDS true)

cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(sm64-tas-scripting
	DESCRIPTION "TASing framework for SM64"
	LANGUAGES CXX
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /openmp")

# Check build type
# https://www.kitware.com/cmake-and-the-default-build-type/
set(SM64_TASFW_DEFAULT_BUILD "Release")

if(EXISTS "${CMAKE_PROJECT_DIR}/.git")
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

# Download conan-cmake
if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
	message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
	file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/0.17.0/conan.cmake"
		"${CMAKE_BINARY_DIR}/conan.cmake"
		TLS_VERIFY ON)
endif()

include("${CMAKE_BINARY_DIR}/conan.cmake")

# Install local packages
# find_package(Python COMPONENTS Interpreter)
# if (${Python_VERSION} VERSION_LESS "3.7.0")
# message(FATAL_ERROR "Upgrade Python (min version 3.7.0)")
# endif()
# execute_process(COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/scripts/conan_installer.py RESULT_VARIABLE RTC)
# if (NOT ${RTC} EQUAL 0)
# message(FATAL_ERROR "Local installer failed")
# endif()

# Dependency config
conan_cmake_configure(
	REQUIRES "nlohmann_json/3.10.5"
	GENERATORS cmake_find_package
)

# Check if generator is multi-config
get_property(is_multiconfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

# Setup packages using conan-cmake
message("Generator is multi-config: ${is_multiconfig}")

if(${is_multiconfig})
	foreach(TYPE ${CMAKE_CONFIGURATION_TYPES})
		message(STATUS "Installing Conan data for build type ${TYPE}")
		conan_cmake_autodetect(SM64_TASFW_SETTINGS BUILD_TYPE ${TYPE})
		conan_cmake_install(PATH_OR_REFERENCE .
			BUILD missing
			SETTINGS ${SM64_TASFW_SETTINGS}
			SETTINGS "compiler.cppstd=20"
			INSTALL_FOLDER "${CMAKE_BINARY_DIR}/CMakeFiles/conan_deps"
		)
	endforeach()
else()
	conan_cmake_autodetect(SM64_TASFW_SETTINGS)
	conan_cmake_install(PATH_OR_REFERENCE .
		BUILD missing
		SETTINGS ${SM64_TASFW_SETTINGS}
		SETTINGS "compiler.cppstd=20"
		INSTALL_FOLDER "${CMAKE_BINARY_DIR}/CMakeFiles/conan_deps"
	)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}/CMakeFiles/conan_deps")
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/CMakeFiles/conan_deps")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Import Conan packages
foreach(PACKAGE IN ITEMS nlohmann_json)
	find_package(${PACKAGE} REQUIRED)
endforeach()

# Library components
add_subdirectory(src/lib)
add_subdirectory(src/bin)

set(target_list tasfw-core tasfw-decomp bitfs-turn)