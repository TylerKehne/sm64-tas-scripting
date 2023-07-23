# BitFS turnaround script. Scripts can likely be moved to /lib, given that they will be chained together?

add_executable(tasfw-bitfs-turnaround
	"src/BitFsConfig.cpp"
	"src/BitFsConfig.hpp"
	"src/main.cpp"
)
target_compile_definitions(tasfw-bitfs-turnaround PRIVATE cxx_std_20)
target_link_libraries(tasfw-bitfs-turnaround PRIVATE 
	tasfw::core 
	tasfw::scripts_general 
	tasfw::scripts_bitfs_pyramid_osc 
	tasfw::scripts_bitfs_sc_setup
	nlohmann_json::nlohmann_json
	range-v3::range-v3
	tasfw-scripts-scattershot-bitfs-dr
)
set_target_properties(tasfw-bitfs-turnaround PROPERTIES
	OUTPUT_NAME "bitfs-turn"
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out"
)


add_optimization_flags(tasfw-bitfs-turnaround)

add_custom_target(tasfw-bitfs-turnaround-config ALL
	${CMAKE_COMMAND} -P "${PROJECT_SOURCE_DIR}/cmake/IncrementalCopy.cmake" ${CMAKE_CURRENT_SOURCE_DIR}/config.json "$<TARGET_FILE_DIR:tasfw-bitfs-turnaround>/config.json"
	COMMENT "Copying config.json for bitfs-turn"
	VERBATIM
)