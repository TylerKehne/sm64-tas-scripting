# Small standalone tools. dllcheck: load a libsm64 DLL, run the VerifyLayout script to a
# frame, and report frame-advance and savestate cost measured on the resource itself.
add_executable(tasfw-dllcheck "dllcheck/main.cpp")
target_link_libraries(tasfw-dllcheck PRIVATE tasfw::core tasfw::resources tasfw::scripts_general)
target_compile_features(tasfw-dllcheck PRIVATE cxx_std_20)
set_target_properties(tasfw-dllcheck PROPERTIES
	OUTPUT_NAME "dllcheck"
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out"
)
add_optimization_flags(tasfw-dllcheck)
