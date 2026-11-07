foreach(failure IN ITEMS allocation-failure size-overflow crt-size-overflow
                         invalid-alignment zero-alignment image-size-overflow)
    execute_process(
        COMMAND "${ALLOCATOR_TEST}" "--probe-${failure}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 15)
    if(NOT "${result}" STREQUAL "86")
        message(FATAL_ERROR "${failure}: expected abort exit 86, got ${result}\n${output}\n${error}")
    endif()
endforeach()
