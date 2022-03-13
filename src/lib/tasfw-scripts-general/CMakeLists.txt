# Dumping ground for all scripts, categorized into headers.
add_library(tasfw-scripts-general
	"src/TryHackedWalkOutOfBounds.cpp"
	"src/GetMinimumDownhillWalkingAngle.cpp"
	"src/BrakeToIdle.cpp"
)

target_include_directories(tasfw-scripts-general PUBLIC include)
target_compile_features(tasfw-scripts-general PUBLIC cxx_std_20)
target_link_libraries(tasfw-scripts-general PUBLIC tasfw::decomp tasfw::core)

include(MatchArchFlag)
match_arch_and_ipo(tasfw-scripts-general)

add_library(tasfw::scripts_general ALIAS tasfw-scripts-general)