function(horizon_msvc_build_environment)
    if(NOT WIN32 OR NOT CMAKE_GENERATOR MATCHES "^Ninja" OR
       NOT DEFINED HORIZON_DEV_BUILD_ENVIRONMENT)
        return()
    endif()

    # Configure and Build may run in different IDE processes and code pages.
    # Keep the selected toolchain's environment with this binary directory.
    get_filename_component(_environment_dir "${HORIZON_DEV_BUILD_ENVIRONMENT}" DIRECTORY)
    set(_snapshot "${CMAKE_BINARY_DIR}/CMakeFiles/horizon-build-environment.json")
    configure_file("${_environment_dir}/dev_build_environment.json" "${_snapshot}" COPYONLY)
    set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/run_build_tool.py")

    foreach(_language C CXX)
        if(NOT CMAKE_${_language}_COMPILER_ID STREQUAL "MSVC")
            continue()
        endif()
        set(_probe "${CMAKE_BINARY_DIR}/CMakeFiles/horizon-${_language}-includes")
        file(MAKE_DIRECTORY "${_probe}")
        file(WRITE "${_probe}/dependency.h" "// Header dependency probe\n")
        file(WRITE "${_probe}/dependency.c" "#include \"dependency.h\"\n")
        execute_process(
            COMMAND "${CMAKE_${_language}_COMPILER}" /nologo /showIncludes /c
                "${_probe}/dependency.c" "/Fo${_probe}/dependency.obj"
            OUTPUT_FILE "${_probe}/output.txt" ERROR_FILE "${_probe}/error.txt"
            RESULT_VARIABLE _result)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "MSVC include-prefix probe failed: ${_probe}/error.txt")
        endif()
        file(READ "${_probe}/output.txt" _output)
        string(REPLACE "\n" ";" _lines "${_output}")
        set(_prefix_hex "")
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "dependency[.]h\r?$")
                # Match ASCII delimiters, preserving localized prefix bytes.
                string(REGEX MATCH "^.*: +" _prefix "${_line}")
                string(HEX "${_prefix}" _prefix_hex)
                break()
            endif()
        endforeach()
        if(_prefix_hex STREQUAL "")
            message(FATAL_ERROR "MSVC include-prefix probe produced no dependency: ${_probe}/output.txt")
        endif()
        foreach(_stage COMPILER LINKER)
            set(CMAKE_${_language}_${_stage}_LAUNCHER
                "${HORIZON_DEV_PYTHON}" "${_script}" "${_snapshot}" "${_prefix_hex}"
                ${CMAKE_${_language}_${_stage}_LAUNCHER} PARENT_SCOPE)
        endforeach()
        set(CMAKE_${_language}_CL_SHOWINCLUDES_PREFIX "Note: including file: " PARENT_SCOPE)
    endforeach()
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file: " PARENT_SCOPE)
endfunction()
