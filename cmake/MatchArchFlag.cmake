# Check for -march=native, with MSVC emulation
include(CheckCXXCompilerFlag)

if(NOT _arch_flag)
	if(${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
		execute_process(
			COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/scripts/msvc_arch_support.py
			RESULT_VARIABLE RTC
			OUTPUT_VARIABLE msvc_arch_opt
		)

		if(${RTC} EQUAL 0)
			set(_arch_flag "${msvc_arch_opt}" CACHE INTERNAL "Architecture optimization flag.")
		else()
			set(_arch_flag "" CACHE INTERNAL "Architecture optimization flag.")
		endif()
	else()
		check_cxx_compiler_flag("-march=native" has_march_native)

		if(${has_march_native})
			set(_arch_flag "-march=native" CACHE INTERNAL "Architecture optimization flag.")
		else()
			set(_arch_flag "" CACHE INTERNAL "Architecture optimization flag.")
		endif()
	endif()
endif()

# Check for IPO/LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported LANGUAGES CXX)

macro(match_arch_and_ipo target)
	if (_arch_flag)
		target_compile_options(${target} PUBLIC ${_arch_flag})
	endif()
	if (_ipo_supported)
		set_target_properties(${target} PROPERTIES
			INTERPROCEDURAL_OPTIMIZATION yes
		)
	endif()
endmacro()