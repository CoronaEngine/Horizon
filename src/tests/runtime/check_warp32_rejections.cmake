foreach(case loop callable barrier short-circuit)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env HORIZON_FORCE_WARP32_FALLBACK=1
                "${WARP32_TEST}" --case "${case}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
    if("${result}" STREQUAL "0" OR "${result}" MATCHES "timeout" OR
       NOT "${output}${error}" MATCHES "warp32 fallback does not support")
        message(FATAL_ERROR "Expected explicit warp32 rejection for ${case}: ${result}\n${output}\n${error}")
    endif()
    message(STATUS "warp32 fallback explicitly rejected ${case}")
endforeach()
