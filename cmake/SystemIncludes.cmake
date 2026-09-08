# Dependencies fetched by CMake (nlohmann/json, doctest, Google Benchmark) are compiled
# inside first-party translation units, so under TASFW_WARNINGS_AS_ERRORS a warning in one
# of their headers would fail the build for a defect that is not ours. The first CI run with
# the option on did exactly that: the runners' newer compilers warned in doctest (MSVC) and
# in nlohmann/json's `operator "" _json` (clang-cl) while the local toolchains were silent
# (docs/compilers.md). Re-declaring a dependency's interface include directories as system
# directories makes every compiler ignore warnings from them: -isystem on GCC and Clang,
# -imsvc on clang-cl, -external:I plus -external:W0 on MSVC (CMake adds the latter itself).
# INTERFACE_SYSTEM_INCLUDE_DIRECTORIES works on every CMake this project accepts; the
# SYSTEM target property that does the same needs 3.25.

function(tasfw_system_includes)
	foreach(target IN LISTS ARGN)
		if(NOT TARGET ${target})
			message(FATAL_ERROR "tasfw_system_includes: no target named ${target}")
		endif()
		get_target_property(dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
		if(dirs)
			set_property(TARGET ${target} APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${dirs})
		endif()
	endforeach()
endfunction()
