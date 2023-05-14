# Check for -march=native, with MSVC emulation
include(CheckCXXCompilerFlag)

function(msvc_arch_check)
	try_run(run_result compile_result "${PROJECT_BINARY_DIR}/CMakeFiles/_arch_detect" "arch_detect/arch_detect.cpp" RUN_OUTPUT_VARIABLE msvc_flag)

	if(${run_result} EQUAL 0)
		set(_arch_flag "${msvc_flag}" CACHE INTERNAL "Architecture optimization flag.")
	else()
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
	if(${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
		msvc_arch_check()
	else()
		generic_arch_check()
	endif()
endif()

# Check for IPO/LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported LANGUAGES CXX)

# Check for OpenMP
find_package(OpenMP REQUIRED)

function(add_optimization_flags target)
	check_cxx_compiler_flag("-Wno-missing-requires" has_missing_requires_warning)
	get_target_property(target_type ${target} TYPE)
	message("Target type: ${target_type}")
	if (target_type STREQUAL "INTERFACE_LIBRARY")
		# for header-only libraries
		if(_arch_flag)
			target_compile_options(${target} INTERFACE ${_arch_flag})
		endif()

		# add OpenMP
		target_link_libraries(${target} INTERFACE OpenMP::OpenMP_CXX)
	else()
		# for libraries with compiled sources
		if(_arch_flag)
			target_compile_options(${target} PUBLIC ${_arch_flag})
		endif()

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
