foreach(probe IN ITEMS invalid-data-type invalid-vector-element invalid-matrix-dimension)
    string(REPLACE "-" " " diagnostic "${probe}")
    execute_process(
        COMMAND "${PARSE_TYPE_TEST}" --invalid-case "${probe}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 15)
    string(FIND "${output}${error}" "${diagnostic}" position)
    if("${result}" STREQUAL "0" OR "${result}" MATCHES "timeout" OR position EQUAL -1)
        message(FATAL_ERROR "${probe}: expected parser rejection, got ${result}\n${output}\n${error}")
    endif()
endforeach()
