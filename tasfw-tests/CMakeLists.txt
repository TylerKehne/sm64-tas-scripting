# Correctness tests (ROADMAP 1.2). doctest is fetched by the top-level CMakeLists.txt (the
# bitfs-turn executable's own optional tests use it too). One file per subject; the
# script engine tests share src/script_fixtures.hpp and the libsm64 tests src/libsm64_env.hpp.
# DLL-free tests always run. The libsm64 tests run only when TASFW_LIBSM64 and TASFW_M64
# point at a DLL and a movie (scripts/test.ps1 sets them when res/ has them).

add_executable(tasfw-tests
	"src/main.cpp"
	"src/test_inputs.cpp"
	"src/test_m64.cpp"
	"src/test_statebin.cpp"
	"src/test_scattershot_hash.cpp"
	"src/test_scattershot_mock.cpp"
	"src/test_slots.cpp"
	"src/test_levelstack.cpp"
	"src/test_framemap.cpp"
	"src/test_script_lifecycle.cpp"
	"src/test_script_saves.cpp"
	"src/test_script_metrics.cpp"
	"src/test_script_compare.cpp"
	"src/test_sharedlib.cpp"
	"src/test_sm64_layout.cpp"
	"src/test_libsm64_smoke.cpp"
	"src/test_libsm64_savemodes.cpp"
	"src/test_libsm64_pyramid.cpp"
	"src/test_pipeline.cpp"
	"src/test_visualization.cpp"
)
target_link_libraries(tasfw-tests PRIVATE tasfw::core tasfw::resources tasfw::scattershot tasfw::testing tasfw::scripts_general tasfw::bitfs_pipeline doctest::doctest)
target_compile_features(tasfw-tests PRIVATE cxx_std_23)
set_target_properties(tasfw-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out")
add_optimization_flags(tasfw-tests)

# Clang 21 with libstdc++ 15: doctest's `#include <ciso646>` (its libc++ probe, still there in
# 2.4.12) trips the library's "<ciso646> is not a standard header since C++20" #warning, which
# Clang reports even from system headers and -Werror then rejects. Not a generator expression:
# $<CXX_COMPILER_FRONTEND_VARIANT> needs CMake 3.30 and Ubuntu 24.04 has 3.28. docs/compilers.md.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	target_compile_options(tasfw-tests PRIVATE "-Wno-#warnings")
endif()

add_test(NAME tasfw-tests COMMAND tasfw-tests)
