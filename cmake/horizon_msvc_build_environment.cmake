function(horizon_msvc_build_environment)
    if(NOT WIN32 OR NOT CMAKE_GENERATOR MATCHES "^Ninja" OR
       NOT DEFINED HORIZON_DEV_BUILD_ENVIRONMENT)
        return()
    endif()

    foreach(_language C CXX)
        if(NOT CMAKE_${_language}_COMPILER_ID STREQUAL "MSVC")
            continue()
        endif()
        # Each build directory owns a snapshot, even when Conan generators
        # are shared with another IDE profile for the same configuration.
        set(_snapshot "${CMAKE_BINARY_DIR}/CMakeFiles/horizon-${_language}-build-environment.cmake")
        configure_file("${HORIZON_DEV_BUILD_ENVIRONMENT}" "${_snapshot}" COPYONLY)
        file(APPEND "${_snapshot}"
            "set(HORIZON_MSVC_SHOWINCLUDES_PREFIX [==[${CMAKE_${_language}_CL_SHOWINCLUDES_PREFIX}]==])\n")
        set(_launcher "${CMAKE_COMMAND}"
            "-DHORIZON_BUILD_ENVIRONMENT=${_snapshot}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/horizon_run_build_tool.cmake" --)
        foreach(_stage COMPILER LINKER)
            set(CMAKE_${_language}_${_stage}_LAUNCHER
                ${_launcher} ${CMAKE_${_language}_${_stage}_LAUNCHER} PARENT_SCOPE)
        endforeach()
        # ASCII remains identical when Configure and Build have different
        # console encodings. The launcher normalizes the compiler's prefix.
        set(CMAKE_${_language}_CL_SHOWINCLUDES_PREFIX "Note: including file: " PARENT_SCOPE)
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file: " PARENT_SCOPE)
    endforeach()
endfunction()
