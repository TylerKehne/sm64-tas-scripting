#!/usr/bin/env -S cmake -P
if (NOT ${CMAKE_ARGC} EQUAL 5)
	message(FATAL_ERROR "Usage: ${CMAKE_ARGV0} -P ${CMAKE_ARGV2} <src> <dst>")
endif()

message(STATUS "Copying ${CMAKE_ARGV3} to ${CMAKE_ARGV4}")

if (NOT EXISTS ${CMAKE_ARGV3})
	execute_process(COMMAND cmake -E copy ${CMAKE_ARGV3} ${CMAKE_ARGV4})
else()
	execute_process(COMMAND cmake -E copy_if_different ${CMAKE_ARGV3} ${CMAKE_ARGV4})
endif()