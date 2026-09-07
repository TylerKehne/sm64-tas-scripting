# Correctness tests (ROADMAP 1.2). doctest via FetchContent.
# DLL-free tests always run. The libsm64 smoke test runs only when TASFW_LIBSM64 and
# TASFW_M64 point at a DLL and a movie (scripts/test.ps1 sets them when res/ has them).

include(FetchContent)

set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(doctest
	DOWNLOAD_EXTRACT_TIMESTAMP ON
	URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
)
FetchContent_MakeAvailable(doctest)

add_executable(tasfw-tests
	"src/main.cpp"
	"src/test_inputs.cpp"
	"src/test_m64.cpp"
	"src/test_statebin.cpp"
	"src/test_slots.cpp"
	"src/test_script.cpp"
	"src/test_libsm64.cpp"
)
target_link_libraries(tasfw-tests PRIVATE tasfw::core tasfw::resources tasfw::scattershot tasfw::testing doctest::doctest)
target_compile_features(tasfw-tests PRIVATE cxx_std_20)
set_target_properties(tasfw-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/out")
add_optimization_flags(tasfw-tests)

add_test(NAME tasfw-tests COMMAND tasfw-tests)
