# gt4_host --replay, end to end: the capture written by gt4_gs_backend_test (a VU1-drawn triangle and a
# PATH3 sprite) is replayed headless at 640 x 448 with the "original" preset, captured, and compared by
# pixel (gt4_gs_backend_test --check-ppm). Skipped (77) when the capture is missing (no suitable GPU).
#   cmake -DHOST=gt4_host -DCHECKER=gt4_gs_backend_test -DOUTPUT=dir -P host_replay.cmake
set(capture "${OUTPUT}/replay.cap")
set(image "${OUTPUT}/replay.ppm")
if(NOT EXISTS "${capture}")
  message(STATUS "host_replay: ${capture} missing (gs_backend skipped); skipping")
  cmake_language(EXIT 77)
endif()
file(REMOVE "${image}")
execute_process(
  COMMAND "${HOST}" --headless --replay "${capture}" --frames 2 --size 640x448 --preset original
          --set aspect.ratio=window --set debug.overlay=off --capture "${image}"
  RESULT_VARIABLE host_result OUTPUT_VARIABLE host_output ERROR_VARIABLE host_output)
message(STATUS "${host_output}")
if(NOT host_result EQUAL 0)
  message(FATAL_ERROR "gt4_host --replay failed (${host_result})")
endif()
execute_process(COMMAND "${CHECKER}" --check-ppm "${image}" RESULT_VARIABLE check_result)
if(NOT check_result EQUAL 0)
  message(FATAL_ERROR "the replayed frame does not match")
endif()
