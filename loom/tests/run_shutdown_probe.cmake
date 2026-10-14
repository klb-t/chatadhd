# Runs loom_shutdown_probe on a fresh data directory (ctest capi.shutdown_cycle).
file(REMOVE_RECURSE "${DIR}")
execute_process(COMMAND "${PROBE}" "${DIR}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("${out}${err}")
file(REMOVE_RECURSE "${DIR}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "loom_shutdown_probe failed: ${rc}")
endif()
