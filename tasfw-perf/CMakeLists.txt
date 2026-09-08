# Performance suite. Tier A (DLL-free microbenchmarks) lives here; see docs/performance.md.
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
)
target_include_directories(tasfw-perf PRIVATE src)
target_link_libraries(tasfw-perf PRIVATE tasfw::core tasfw::resources tasfw::scattershot tasfw::testing benchmark::benchmark)
target_compile_features(tasfw-perf PRIVATE cxx_std_20)
set_target_properties(tasfw-perf PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out")

add_optimization_flags(tasfw-perf)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	message(STATUS "tasfw-perf: Debug build; benchmark numbers from this configuration are not meaningful.")
endif()
