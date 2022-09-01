# Dumping ground for all scripts, categorized into headers.
add_library(tasfw-bitfs-sc-setup
	"src/BitFsScApproach.cpp"
	"src/BitFsScApproach_AttemptDr.cpp"
	"src/BitFsScApproach_AttemptDr_BF.cpp")

target_include_directories(tasfw-bitfs-sc-setup PUBLIC include)
target_compile_features(tasfw-bitfs-sc-setup PUBLIC cxx_std_20)
target_link_libraries(tasfw-bitfs-sc-setup PUBLIC tasfw::decomp tasfw::core tasfw::scripts_general tasfw::resources tasfw-scripts-bitfs-pyramid-osc)

include(MatchArchFlag)
match_arch_and_ipo(tasfw-bitfs-sc-setup)

add_library(tasfw::bitfs-sc-setup ALIAS tasfw-bitfs-sc-setup)