# Test-support code shared by tasfw-tests and tasfw-perf (header-only).
add_library(tasfw-testing INTERFACE)
target_include_directories(tasfw-testing INTERFACE inc)
target_link_libraries(tasfw-testing INTERFACE tasfw::core)
add_library(tasfw::testing ALIAS tasfw-testing)
