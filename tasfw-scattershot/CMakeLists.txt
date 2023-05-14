# Header-only libraries use INTERFACE
add_library(tasfw-scattershot INTERFACE)
target_include_directories(tasfw-scattershot INTERFACE inc)
target_link_libraries(tasfw-scattershot INTERFACE tasfw-core)
add_optimization_flags(tasfw-scattershot)

add_library(tasfw::scattershot ALIAS tasfw-scattershot)