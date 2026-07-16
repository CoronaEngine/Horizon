function(horizon_dev_bootstrap)
    if(NOT DEFINED HORIZON_DEV_CONFIGURATION OR HORIZON_DEV_CONFIGURATION STREQUAL "")
        set(HORIZON_DEV_CONFIGURATION "RelWithDebInfo" CACHE STRING "Horizon developer configuration")
    endif()

    string(TOLOWER "${HORIZON_DEV_CONFIGURATION}" _horizon_configuration_slug)
    set(_horizon_toolchain
        "${CMAKE_CURRENT_SOURCE_DIR}/build/conan/${_horizon_configuration_slug}/generators/conan_toolchain.cmake")
    set(_horizon_build_environment
        "${CMAKE_CURRENT_SOURCE_DIR}/build/conan/${_horizon_configuration_slug}/generators/dev_build_environment.cmake")

    if(NOT DEFINED ENV{CORONA_DEV_BOOTSTRAP_ACTIVE})
        find_program(_horizon_uv uv REQUIRED)
        execute_process(
            COMMAND "${_horizon_uv}" run --frozen python tools/dev.py _bootstrap
                    --configuration "${HORIZON_DEV_CONFIGURATION}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _horizon_bootstrap_result
            OUTPUT_VARIABLE _horizon_bootstrap_stdout
            ERROR_VARIABLE _horizon_bootstrap_stderr
        )
        if(NOT _horizon_bootstrap_result EQUAL 0)
            message(FATAL_ERROR
                "Horizon dependency bootstrap failed (${_horizon_bootstrap_result}).\n"
                "${_horizon_bootstrap_stdout}\n${_horizon_bootstrap_stderr}")
        endif()
    endif()

    if(NOT EXISTS "${_horizon_toolchain}")
        message(FATAL_ERROR "Horizon Conan toolchain was not generated: ${_horizon_toolchain}")
    endif()
    if(NOT EXISTS "${_horizon_build_environment}")
        message(FATAL_ERROR "Horizon build environment was not generated: ${_horizon_build_environment}")
    endif()
    include("${_horizon_build_environment}")
    set(CMAKE_TOOLCHAIN_FILE "${_horizon_toolchain}" CACHE FILEPATH "Conan toolchain" FORCE)
endfunction()
