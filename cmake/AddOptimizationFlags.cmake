# Check for -march=native, with MSVC emulation
include(CheckCXXCompilerFlag)

function(msvc_arch_check)
	message(STATUS "Detecting MSVC /arch flag")
	# The probe source lives next to this module; try_run needs an absolute path here.
	try_run(run_result compile_result "${PROJECT_BINARY_DIR}/CMakeFiles/_arch_detect"
		"${CMAKE_CURRENT_LIST_DIR}/arch_detect/arch_detect.cpp" RUN_OUTPUT_VARIABLE msvc_flag)
	string(STRIP "${msvc_flag}" msvc_flag)
	message(STATUS "Detecting MSVC /arch flag - done (${msvc_flag})")

	if(compile_result AND run_result EQUAL 0)
		set(_arch_flag "${msvc_flag}" CACHE INTERNAL "Architecture optimization flag.")
	else()
		message(WARNING "MSVC /arch probe failed to compile or run; building without an /arch flag.")
		set(_arch_flag "" CACHE INTERNAL "Architecture optimization flag.")
	endif()
endfunction()

function(generic_arch_check)
	check_cxx_compiler_flag("-march=native" has_march_native)

	if(${has_march_native})
		set(_arch_flag "-march=native" CACHE INTERNAL "Architecture optimization flag.")
	else()
		set(_arch_flag "" CACHE INTERNAL "Architecture optimization flag.")
	endif()
endfunction()

if(NOT _arch_flag)
	# Note the quoting: an unquoted ${CMAKE_CXX_COMPILER_ID} expands to the token MSVC, which
	# if() then dereferences as the variable MSVC (= 1), so the comparison was always false and
	# MSVC builds silently got no /arch flag at all. Compare the variable by name instead.
	# clang-cl reports "Clang" with an MSVC front end; it understands -march=native.
	if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
		msvc_arch_check()
	else()
		generic_arch_check()
	endif()
endif()

# Floating-point determinism. The framework re-implements pieces of game physics in C++
# (PyramidUpdate) and must produce bit-identical floats to the game DLL and to itself across
# compilers. With an AVX2+ target, GCC (-ffp-contract=fast) and Clang (-ffp-contract=on)
# fuse a*b+c into FMA by default, which changes rounding. MSVC does not contract under its
# default /fp:precise. Force the same behavior everywhere.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	set(_fp_flags "/fp:precise")
elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	# clang-cl: GNU-style flags must go through /clang:, otherwise clang-cl *ignores* them with
	# only a -Wunknown-argument warning and contraction stays on (its /fp:precise means
	# -ffp-contract=on). Found 2026-09-07; see docs/compilers.md.
	set(_fp_flags "/clang:-ffp-contract=off")
else()
	set(_fp_flags "-ffp-contract=off")
endif()

# Check for IPO/LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported LANGUAGES CXX)

# Check for OpenMP
find_package(OpenMP REQUIRED)

function(add_optimization_flags target)
	check_cxx_compiler_flag("-Wno-missing-requires" has_missing_requires_warning)
	get_target_property(target_type ${target} TYPE)
	if (target_type STREQUAL "INTERFACE_LIBRARY")
		# for header-only libraries
		if(_arch_flag)
			target_compile_options(${target} INTERFACE ${_arch_flag})
		endif()
		target_compile_options(${target} INTERFACE ${_fp_flags})

		# add OpenMP
		target_link_libraries(${target} INTERFACE OpenMP::OpenMP_CXX)
	else()
		# for libraries with compiled sources
		if(_arch_flag)
			target_compile_options(${target} PUBLIC ${_arch_flag})
		endif()
		target_compile_options(${target} PUBLIC ${_fp_flags})

		# add LTO
		if(_ipo_supported)
			set_target_properties(${target} PROPERTIES
				INTERPROCEDURAL_OPTIMIZATION yes
				)
		endif()

		# add OpenMP
		target_link_libraries(${target} PUBLIC OpenMP::OpenMP_CXX)

		# disable a warning on GCC/Clang (-Wno-missing-requires)
		if (${has_missing_requires_warning})
			target_compile_options(${target} PRIVATE "-Wno-missing-requires")
		endif()
	endif()

	# add -march=native type flag

endfunction()
