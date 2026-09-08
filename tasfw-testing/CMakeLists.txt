# Test-support code shared by tasfw-tests and tasfw-perf (header-only).
add_library(tasfw-testing INTERFACE)
target_include_directories(tasfw-testing INTERFACE inc)
target_link_libraries(tasfw-testing INTERFACE tasfw::core)
# Env.hpp uses std::getenv, which MSVC's CRT marks deprecated (clang-cl warns at its default
# level; an error under TASFW_WARNINGS_AS_ERRORS). Tests and benchmarks are not hardened
# code, so silence the CRT for their translation units instead of forking to _dupenv_s.
if(MSVC)
	target_compile_definitions(tasfw-testing INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()
add_library(tasfw::testing ALIAS tasfw-testing)
