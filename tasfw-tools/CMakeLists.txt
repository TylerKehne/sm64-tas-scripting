# Small standalone tools. dllcheck: load a libsm64 DLL, run the VerifyLayout script to a
# frame, and report frame-advance and savestate cost measured on the resource itself.
add_executable(tasfw-dllcheck "dllcheck/main.cpp")
target_link_libraries(tasfw-dllcheck PRIVATE tasfw::core tasfw::resources tasfw::scripts_general)
target_compile_features(tasfw-dllcheck PRIVATE cxx_std_23)
set_target_properties(tasfw-dllcheck PROPERTIES
	OUTPUT_NAME "dllcheck"
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out"
)
add_optimization_flags(tasfw-dllcheck)

# m64splice: a movie for one game version out of two (the LevelTransitions and SpliceMovie
# scripts): the first movie up to the frame it enters a level, the second from its own.
add_executable(tasfw-m64splice "m64splice/main.cpp")
target_link_libraries(tasfw-m64splice PRIVATE tasfw::core tasfw::resources tasfw::scripts_general)
target_compile_features(tasfw-m64splice PRIVATE cxx_std_23)
set_target_properties(tasfw-m64splice PROPERTIES
	OUTPUT_NAME "m64splice"
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out"
)
add_optimization_flags(tasfw-m64splice)
