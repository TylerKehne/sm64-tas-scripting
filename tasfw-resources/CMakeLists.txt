add_library(tasfw-resources STATIC
	"src/LibSm64.cpp"
	"src/PyramidUpdate.cpp"
	"src/PyramidUpdate_Mario.cpp"
)
target_include_directories(tasfw-resources PUBLIC inc)
target_link_libraries(tasfw-resources PUBLIC tasfw::core)
add_optimization_flags(tasfw-resources)
add_library(tasfw::resources ALIAS tasfw-resources)
