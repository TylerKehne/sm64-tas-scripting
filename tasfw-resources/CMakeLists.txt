add_library(tasfw-resources STATIC
	"src/BitfsCorner.cpp"
	"src/LibSm64.cpp"
)
target_include_directories(tasfw-resources PUBLIC inc)
# The BitFS corner's copied game files (src/bitfs-corner/decomp, ROADMAP 4.13) are the
# decomp's own text and warn like any external code; found through a system include
# path, their warnings do not count (cmake/SystemIncludes.cmake says how each compiler
# does that), while the resource's own files stay at the project's level.
target_include_directories(tasfw-resources SYSTEM PRIVATE src/bitfs-corner)
target_link_libraries(tasfw-resources PUBLIC tasfw::core)
add_optimization_flags(tasfw-resources)
add_library(tasfw::resources ALIAS tasfw-resources)
