# Runs `topo --check` on an invalid fixture and asserts the DIAGNOSTIC COUNT
# of a given message is within [MIN_COUNT, MAX_COUNT]. Error recovery must
# yield a bounded diagnostic set (first error + resync), not a wall of
# same-position repeats — ctest's line-based regex properties cannot count
# occurrences across lines, so counting lives in this cmake -P script
# (cross-platform, no shell pipeline).
#
# Usage: cmake -DTOPO_BIN=<path> -DFIXTURE=<file.topo>
#              -DPATTERN=<regex> -DMIN_COUNT=<n> -DMAX_COUNT=<n>
#              -P count_diagnostics.cmake

if(NOT DEFINED TOPO_BIN OR NOT DEFINED FIXTURE OR NOT DEFINED PATTERN
   OR NOT DEFINED MIN_COUNT OR NOT DEFINED MAX_COUNT)
    message(FATAL_ERROR "count_diagnostics.cmake requires TOPO_BIN, FIXTURE, PATTERN, MIN_COUNT, MAX_COUNT")
endif()

execute_process(
    COMMAND "${TOPO_BIN}" --check "${FIXTURE}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)

if(rc EQUAL 0)
    message(FATAL_ERROR "expected '${TOPO_BIN} --check' to reject invalid fixture '${FIXTURE}' (exit 0)")
endif()

string(REGEX MATCHALL "${PATTERN}" matches "${out}${err}")
list(LENGTH matches count)

if(count LESS MIN_COUNT)
    message(FATAL_ERROR "expected at least ${MIN_COUNT} '${PATTERN}' diagnostic(s) in '${FIXTURE}', got ${count}\n${out}${err}")
endif()
if(count GREATER MAX_COUNT)
    message(FATAL_ERROR "'${PATTERN}' diagnostic count ${count} exceeds bound ${MAX_COUNT} in '${FIXTURE}'\n${out}${err}")
endif()

message(STATUS "diagnostic count ${count} within [${MIN_COUNT}, ${MAX_COUNT}] for '${FIXTURE}'")
