# bitfs-turn: the BitFS squish-cancel pipeline as config-selected stages (ROADMAP 1.4).
#
# The DLL-free part (config schema, solution files, argument helpers) is a library so that
# tasfw-tests can cover it without libsm64; the stages and the executable need the game.

add_library(tasfw-bitfs-pipeline STATIC
	"src/PipelineConfig.cpp"
	"src/PipelineConfig.hpp"
	"src/SolutionSet.cpp"
	"src/SolutionSet.hpp"
	"src/StageArgs.cpp"
	"src/StageArgs.hpp"
)
target_include_directories(tasfw-bitfs-pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tasfw-bitfs-pipeline PUBLIC tasfw::core tasfw::resources tasfw::scattershot nlohmann_json::nlohmann_json)
target_compile_features(tasfw-bitfs-pipeline PUBLIC cxx_std_20)
add_library(tasfw::bitfs_pipeline ALIAS tasfw-bitfs-pipeline)
add_optimization_flags(tasfw-bitfs-pipeline)

add_executable(tasfw-bitfs-turnaround
	"src/main.cpp"
	"src/Stages.cpp"
	"src/Stages.hpp"
	"src/ExportSolutions.hpp"
	"src/ProcessCycles.cpp"
	"src/ProcessCycles.hpp"
	"src/SelfPath.cpp"
	"src/SelfPath.hpp"
	"src/TiltTargetShot.hpp"
	"src/BitfsOscFinal.hpp"
)
target_compile_features(tasfw-bitfs-turnaround PRIVATE cxx_std_20)
target_link_libraries(tasfw-bitfs-turnaround PRIVATE
	tasfw::bitfs_pipeline
	tasfw::core
	tasfw::resources
	tasfw::scripts_general
	tasfw::scripts_bitfs_pyramid_osc
	tasfw::scripts_bitfs_sc_setup
	tasfw-scripts-scattershot-bitfs-dr
)
set_target_properties(tasfw-bitfs-turnaround PROPERTIES
	OUTPUT_NAME "bitfs-turn"
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out"
)
add_optimization_flags(tasfw-bitfs-turnaround)

# config.json next to the executable, with "baseDirectory" set to this directory so that
# the committed file's relative paths keep working from the build tree.
set(_config_out "${PROJECT_BINARY_DIR}/out/config.json")
add_custom_command(
	OUTPUT "${_config_out}"
	COMMAND ${CMAKE_COMMAND}
		-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/config.json
		-DOUTPUT=${_config_out}
		-DBASE=${CMAKE_CURRENT_SOURCE_DIR}
		-P ${PROJECT_SOURCE_DIR}/cmake/StampConfig.cmake
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config.json" "${PROJECT_SOURCE_DIR}/cmake/StampConfig.cmake"
	COMMENT "Writing config.json for bitfs-turn"
	VERBATIM
)
add_custom_target(tasfw-bitfs-turnaround-config ALL DEPENDS "${_config_out}")
