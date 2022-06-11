# Dumping ground for all resources, categorized into headers.
add_library(tasfw-resources
	"src/LibSm64.cpp"
	"src/PyramidUpdate.cpp"
)

target_include_directories(tasfw-resources PUBLIC include)
target_compile_features(tasfw-resources PUBLIC cxx_std_20)
target_link_libraries(tasfw-resources PUBLIC tasfw::core tasfw::decomp)

include(MatchArchFlag)
match_arch_and_ipo(tasfw-resources)

add_library(tasfw::resources ALIAS tasfw-resources)