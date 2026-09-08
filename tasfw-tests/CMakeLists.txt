# Correctness tests (ROADMAP 1.2). doctest via FetchContent.
# DLL-free tests always run. The libsm64 smoke test runs only when TASFW_LIBSM64 and
# TASFW_M64 point at a DLL and a movie (scripts/test.ps1 sets them when res/ has them).

include(FetchContent)

set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(doctest
	DOWNLOAD_EXTRACT_TIMESTAMP ON
	DOWNLOAD_DIR "${TASFW_DOWNLOAD_DIR}"
	DOWNLOAD_NAME doctest-2.4.11.tar.gz
	URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
	URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
)
FetchContent_MakeAvailable(doctest)
tasfw_system_includes(doctest)

add_executable(tasfw-tests
	"src/main.cpp"
	"src/test_inputs.cpp"
	"src/test_m64.cpp"
	"src/test_statebin.cpp"
	"src/test_slots.cpp"
	"src/test_levelstack.cpp"
	"src/test_script.cpp"
	"src/test_sharedlib.cpp"
	"src/test_libsm64.cpp"
	"src/test_pipeline.cpp"
)
target_link_libraries(tasfw-tests PRIVATE tasfw::core tasfw::resources tasfw::scattershot tasfw::testing tasfw::bitfs_pipeline doctest::doctest)
target_compile_features(tasfw-tests PRIVATE cxx_std_20)
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
