function(horizon_configure_ninja)
    if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
        return()
    endif()
    # Run before project(): a stale cache must not prevent generator startup.
    if(CMAKE_MAKE_PROGRAM)
        execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
            RESULT_VARIABLE _ninja_result OUTPUT_VARIABLE _ninja_version
            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 30)
        if(_ninja_result STREQUAL "0" AND _ninja_version MATCHES "^[0-9]+\\.[0-9]+")
            return()
        endif()
        message(STATUS "[Ninja] Replacing unusable CMAKE_MAKE_PROGRAM: ${CMAKE_MAKE_PROGRAM}")
    endif()
    # The Python bootstrap has already checked/provisioned Ninja and exported PATH.
    find_program(_horizon_ninja NAMES ninja NO_CACHE REQUIRED)
    set(CMAKE_MAKE_PROGRAM "${_horizon_ninja}" CACHE FILEPATH "Ninja build executable" FORCE)
endfunction()
