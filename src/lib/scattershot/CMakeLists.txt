add_library(scattershot
	"src/Scattershot.cpp")

target_include_directories(scattershot PUBLIC include)
target_compile_features(scattershot PUBLIC cxx_std_20)
target_link_libraries(scattershot PUBLIC
	tasfw::decomp
	tasfw::core
	tasfw::resources)


add_optimization_flags(scattershot)

add_library(tasfw::scattershot ALIAS scattershot)