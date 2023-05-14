# This module should contain all decomp-copied classes.
add_library(tasfw-decomp
	"src/Math.cpp"
	"src/Pyramid.cpp"
	"src/Surface.cpp"
)
target_include_directories(tasfw-decomp PUBLIC include)
target_compile_features(tasfw-decomp PUBLIC cxx_std_20)


add_optimization_flags(tasfw-decomp)

add_library(tasfw::decomp ALIAS tasfw-decomp)