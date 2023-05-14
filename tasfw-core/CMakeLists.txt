add_library(tasfw-core STATIC
	"src/resources/LibSm64.cpp"
	"src/resources/PyramidUpdate.cpp"
	"src/resources/PyramidUpdate_Mario.cpp"
	"src/core/SharedLib.cpp"
	"src/core/Inputs.cpp"
	"src/decomp/Pyramid.cpp"
	"src/decomp/Surface.cpp"
	"src/decomp/Math.cpp"
)
target_include_directories(tasfw-core PUBLIC inc)
target_link_libraries(tasfw-core PUBLIC ${CMAKE_DL_LIBS})
target_compile_features(tasfw-core PUBLIC cxx_std_20)

add_optimization_flags(tasfw-core)

add_library(tasfw::core ALIAS tasfw-core)