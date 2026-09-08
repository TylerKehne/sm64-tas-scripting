# TASFW_WARNINGS_AS_ERRORS (ROADMAP 1.5): /WX or -Werror on every target this repository
# defines, found by walking the directory tree from the root so that a new target is covered
# without being registered anywhere. Targets from FetchContent dependencies (nlohmann/json,
# doctest, Google Benchmark) are left alone: their warnings are not ours to fix. Interface
# libraries get nothing; their headers are compiled inside the consumers, all first-party.
# CI turns the option on (.github/workflows/build.yml); locally it is off by default so that a
# newer compiler's new warnings cannot block a build, and docs/compilers.md still requires a
# change to be warning-free on every compiler before it is done.

function(_tasfw_collect_first_party_targets out_var dir)
	set(result "")
	get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
	foreach(sub IN LISTS subdirs)
		# A dependency's source tree is under some _deps directory (the build tree's, or
		# wherever FETCHCONTENT_SOURCE_DIR_* points), or outside this repository altogether.
		cmake_path(IS_PREFIX PROJECT_SOURCE_DIR "${sub}" NORMALIZE in_repo)
		if(NOT in_repo OR sub MATCHES "/_deps/")
			continue()
		endif()
		_tasfw_collect_first_party_targets(sub_targets "${sub}")
		list(APPEND result ${sub_targets})
	endforeach()
	get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
	list(APPEND result ${targets})
	set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

function(tasfw_warnings_as_errors)
	if(MSVC)   # cl and clang-cl
		set(flag /WX)
	else()
		set(flag -Werror)
	endif()
	_tasfw_collect_first_party_targets(targets "${PROJECT_SOURCE_DIR}")
	set(applied "")
	foreach(target IN LISTS targets)
		get_target_property(type ${target} TYPE)
		if(type STREQUAL "INTERFACE_LIBRARY" OR type STREQUAL "UTILITY")
			continue()
		endif()
		target_compile_options(${target} PRIVATE ${flag})
		list(APPEND applied ${target})
	endforeach()
	list(SORT applied)
	list(JOIN applied " " applied)
	message(STATUS "Warnings as errors (${flag}): ${applied}")
endfunction()
