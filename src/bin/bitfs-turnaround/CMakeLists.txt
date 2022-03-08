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
	nlohmann_json::nlohmann_json
)

include(MatchArchFlag)
match_arch_and_ipo(bitfs-turn)