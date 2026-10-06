# Writes INPUT (the bitfs-turn config) to OUTPUT with "baseDirectory" set to BASE. The
# config committed in the source tree uses paths relative to itself; the copy next to the
# executable gets an absolute base so those paths still resolve. The key is inserted as text
# right after the opening brace rather than through string(JSON), which would reject the
# config's // comments and drop them (PipelineConfig::Load ignores comments); BASE is a
# CMake path, forward slashes only. Only rewrites OUTPUT when the content changed.
file(READ "${INPUT}" content)
if(content MATCHES "\"baseDirectory\"")
	message(FATAL_ERROR "${INPUT} already has a baseDirectory; the committed config must not")
endif()
string(REGEX REPLACE "^([ \t\r\n]*{)" "\\1\n\t\"baseDirectory\": \"${BASE}\"," content "${content}")
file(WRITE "${OUTPUT}.tmp" "${content}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUTPUT}.tmp" "${OUTPUT}")
file(REMOVE "${OUTPUT}.tmp")
