# Small standalone tools. dllcheck: load a libsm64 DLL, play a movie to a frame, verify the
# struct layouts against the game, and report frame-advance and savestate cost.
add_executable(tasfw-dllcheck "dllcheck/main.cpp")
target_link_libraries(tasfw-dllcheck PRIVATE tasfw::core tasfw::resources)
target_compile_features(tasfw-dllcheck PRIVATE cxx_std_20)
set_target_properties(tasfw-dllcheck PROPERTIES
	OUTPUT_NAME "dllcheck"
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out"
)
add_optimization_flags(tasfw-dllcheck)
