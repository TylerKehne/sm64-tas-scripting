# Writes INPUT (a JSON file) to OUTPUT with "baseDirectory" set to BASE. The bitfs-turn
# config committed in the source tree uses paths relative to itself; the copy next to the
# executable gets an absolute base so those paths still resolve. Requires CMake 3.19 for
# string(JSON). Only rewrites OUTPUT when the content changed.
file(READ "${INPUT}" content)
string(JSON content SET "${content}" "baseDirectory" "\"${BASE}\"")
file(WRITE "${OUTPUT}.tmp" "${content}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUTPUT}.tmp" "${OUTPUT}")
file(REMOVE "${OUTPUT}.tmp")
