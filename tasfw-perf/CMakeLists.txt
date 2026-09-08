# Performance suite: Tier A (DLL-free microbenchmarks), Tier B (the game DLL) and Tier C
# (framework workloads on the game); see docs/performance.md. Tier D runs bitfs-turn itself.
# Only meaningful in Release / RelWithDebInfo. scripts/perf.ps1 is the supported runner.

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(benchmark
	DOWNLOAD_EXTRACT_TIMESTAMP ON
	URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
)
FetchContent_MakeAvailable(benchmark)
tasfw_system_includes(benchmark benchmark_main)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	# Google Benchmark appends /MP (MSVC's parallel-compile switch) to its own flags under any
	# MSVC-style compiler. clang-cl has no use for it and its driver warns once per source
	# (-Wunused-command-line-argument, 21 warnings). The flag lives in benchmark's directory
	# scope, so its targets are the only clean place to silence it. docs/compilers.md.
	foreach(bench_target IN ITEMS benchmark benchmark_main)
		if(TARGET ${bench_target})
			target_compile_options(${bench_target} PRIVATE -Wno-unused-command-line-argument)
		endif()
	endforeach()
endif()

add_executable(tasfw-perf
	"src/main.cpp"
	"src/alloc_counter.cpp"
	"src/bench_statebin.cpp"
	"src/bench_scattershot.cpp"
	"src/bench_inputs.cpp"
	"src/bench_m64.cpp"
	"src/bench_slots.cpp"
	"src/bench_script.cpp"
	"src/bench_libsm64.cpp"
	"src/bench_framework.cpp"
)
target_include_directories(tasfw-perf PRIVATE src)
target_link_libraries(tasfw-perf PRIVATE
	tasfw::core
	tasfw::resources
	tasfw::scattershot
	tasfw::testing
	tasfw::scripts_bitfs_pyramid_osc
	tasfw-scripts-scattershot-bitfs-dr
	benchmark::benchmark
)
target_compile_features(tasfw-perf PRIVATE cxx_std_20)
set_target_properties(tasfw-perf PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out")

add_optimization_flags(tasfw-perf)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	message(STATUS "tasfw-perf: Debug build; benchmark numbers from this configuration are not meaningful.")
endif()
