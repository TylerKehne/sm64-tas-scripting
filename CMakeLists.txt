set(CMAKE_EXPORT_COMPILE_COMMANDS true)

cmake_minimum_required(VERSION 3.18 FATAL_ERROR)
project(sm64-tas-scripting
	DESCRIPTION "TASing framework for SM64"
	LANGUAGES CXX
)

# Download conan-cmake
if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
	message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
	file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/v0.16.1/conan.cmake"
		"${CMAKE_BINARY_DIR}/conan.cmake"
		EXPECTED_HASH SHA256=396e16d0f5eabdc6a14afddbcfff62a54a7ee75c6da23f32f7a31bc85db23484
		TLS_VERIFY ON)
endif()
include("${CMAKE_BINARY_DIR}/conan.cmake")

# Check build type
# https://www.kitware.com/cmake-and-the-default-build-type/
set(SM64_TASFW_DEFAULT_BUILD "Release")
if (EXISTS "${CMAKE_PROJECT_DIR}/.git")
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

# Install local packages
execute_process(COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/scripts/conan_installer.py)

# Dependency config
conan_cmake_configure(
	REQUIRES "mtap/0.2" "nlohmann_json/3.10.5"
	GENERATORS cmake_find_package
)

# Setup packages using conan-cmake
if(GENERATOR_IS_MULTI_CONFIG)
	foreach (TYPE ${CMAKE_CONFIGURATION_TYPES})
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

# Import Conan packages
foreach(PACKAGE IN ITEMS nlohmann_json mtap)
	find_package(${PACKAGE} REQUIRED)
endforeach()

# TODO: separate library files into a separate target
add_executable(main
	"sm64-tas-scripting/BitFsConfig.cpp"
	"sm64-tas-scripting/BitFsConfig.hpp"
	"sm64-tas-scripting/BitFsPyramidOscillation.cpp"
	"sm64-tas-scripting/BitFsPyramidOscillation_Iteration.cpp"
	"sm64-tas-scripting/BitFsPyramidOscillation_RunDownhill.cpp"
	"sm64-tas-scripting/BitFsPyramidOscillation_TurnAroundAndRunDownhill.cpp"
	"sm64-tas-scripting/BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle.cpp"
	"sm64-tas-scripting/BitFsPyramidOscillation_TurnThenRunDownhill.cpp"
	"sm64-tas-scripting/BrakeToIdle.cpp"
	"sm64-tas-scripting/Camera.hpp"
	"sm64-tas-scripting/Game.cpp"
	"sm64-tas-scripting/Game.hpp"
	"sm64-tas-scripting/GetMinimumDownhillWalkingAngle.cpp"
	"sm64-tas-scripting/Inputs.cpp"
	"sm64-tas-scripting/Inputs.hpp"
	"sm64-tas-scripting/main.cpp"
	"sm64-tas-scripting/math.cpp"
	"sm64-tas-scripting/math.hpp"
	"sm64-tas-scripting/ObjectFields.hpp"
	"sm64-tas-scripting/pyramid.cpp"
	"sm64-tas-scripting/pyramid.hpp"
	"sm64-tas-scripting/Script_BitFsPyramidOscillation.hpp"
	"sm64-tas-scripting/Script.cpp"
	"sm64-tas-scripting/Script_General.hpp"
	"sm64-tas-scripting/Script.hpp"
	"sm64-tas-scripting/SharedLib.cpp"
	"sm64-tas-scripting/SharedLib.hpp"
	"sm64-tas-scripting/Sm64.hpp"
	"sm64-tas-scripting/surface.cpp"
	"sm64-tas-scripting/surface.hpp"
	"sm64-tas-scripting/Trig.cpp"
	"sm64-tas-scripting/Trig.hpp"
	"sm64-tas-scripting/TryHackedWalkOutOfBounds.cpp"
	"sm64-tas-scripting/Types.hpp"
	"sm64-tas-scripting/UltraTypes.hpp"
)
target_compile_features(main PUBLIC cxx_std_20)
target_link_libraries(main nlohmann_json::nlohmann_json mtap::mtap)

set(SM64_TASFW_CONFIG "SM64_TASFW_CONFIG_PATH=\"${CMAKE_CURRENT_LIST_DIR}/local/config.json\"")
target_compile_definitions(main PRIVATE $<$<CONFIG:Debug>:${SM64_TASFW_CONFIG}>)
