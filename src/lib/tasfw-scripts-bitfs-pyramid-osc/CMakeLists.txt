# Dumping ground for all scripts, categorized into headers.
add_library(tasfw-scripts-bitfs-pyramid-osc
	"src/BitFsPyramidOscillation.cpp"
	"src/BitFsPyramidOscillation_Iteration.cpp"
	"src/BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle.cpp"
	"src/BitFsPyramidOscillation_TurnAroundAndRunDownhill.cpp"
	"src/BitFsPyramidOscillation_TurnThenRunDownhill.cpp"
	"src/BitFsPyramidOscillation_RunDownhill.cpp"
)

target_include_directories(tasfw-scripts-bitfs-pyramid-osc PUBLIC include)
target_compile_features(tasfw-scripts-bitfs-pyramid-osc PUBLIC cxx_std_20)
target_link_libraries(tasfw-scripts-bitfs-pyramid-osc PUBLIC tasfw::decomp tasfw::core tasfw::scripts_general)

include(MatchArchFlag)
match_arch_and_ipo(tasfw-scripts-bitfs-pyramid-osc)

add_library(tasfw::scripts_bitfs_pyramid_osc ALIAS tasfw-scripts-bitfs-pyramid-osc)