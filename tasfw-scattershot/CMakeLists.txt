# Header-only libraries use INTERFACE
add_library(tasfw-scattershot INTERFACE)
target_include_directories(tasfw-scattershot INTERFACE inc)
target_link_libraries(tasfw-scattershot INTERFACE tasfw-core nlohmann_json::nlohmann_json) # json: the viewer's parameters file (Visualization.hpp)
add_optimization_flags(tasfw-scattershot)

add_library(tasfw::scattershot ALIAS tasfw-scattershot)