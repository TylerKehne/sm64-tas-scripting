# BitFS turnaround script. Scripts can likely be moved to /lib, given that they will be chained together?

add_executable(bitfs-turn
	"src/BitFsConfig.cpp"
	"src/BitFsConfig.hpp"
	"src/main.cpp"
)
target_compile_definitions(bitfs-turn PRIVATE cxx_std_20)
target_link_libraries(bitfs-turn PRIVATE 
	tasfw::core 
	tasfw::decomp 
	tasfw::scripts_general 
	tasfw::scripts_bitfs_pyramid_osc 
	tasfw-bitfs-sc-setup
	nlohmann_json::nlohmann_json
)

include(MatchArchFlag)
match_arch_and_ipo(bitfs-turn)

# Slightly janky workaround, would be nice to copy incrementally
add_custom_target(bitfs-turn-config ALL
	${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/config.json "$<TARGET_FILE_DIR:bitfs-turn>/config.json"
	COMMENT "Copying config.json for bitfs-turn"
	VERBATIM
)