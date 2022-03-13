# This module contains base scripting classes.
# Application-specific scripting belongs in a separate lib, or alternatively
# with the main program.
add_library(tasfw-core
	"src/Game.cpp"
	"src/Inputs.cpp"
	"src/Script.cpp"
	"src/SharedLib.cpp"
)
target_compile_features(tasfw-core PUBLIC cxx_std_20)
target_include_directories(tasfw-core PUBLIC include)
target_link_libraries(tasfw-core PUBLIC tasfw::decomp)

include(MatchArchFlag)
match_arch_and_ipo(tasfw-core)

add_library(tasfw::core ALIAS tasfw-core)
