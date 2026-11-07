function(check_capture_rejected probe diagnostic)
    execute_process(
        COMMAND "${CORRECTOR_TEST}" "--probe-${probe}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 15)
    string(FIND "${output}${error}" "${diagnostic}" position)
    if("${result}" STREQUAL "0" OR "${result}" MATCHES "timeout" OR position EQUAL -1)
        message(FATAL_ERROR "${probe}: expected capture rejection, got ${result}\n${output}\n${error}")
    endif()
endfunction()

check_capture_rejected(shared-capture "Shared arrays cannot be captured by callables")
check_capture_rejected(forbidden-capture "must not capture any variables")
