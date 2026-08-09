function(horizon_dev_bootstrap)
    if(NOT DEFINED HORIZON_CONDA_ENV OR HORIZON_CONDA_ENV STREQUAL "")
        set(HORIZON_CONDA_ENV "horizon-dev" CACHE STRING "Conda environment used by Horizon development tools")
    endif()
    if(NOT DEFINED HORIZON_DEV_CONFIGURATION OR HORIZON_DEV_CONFIGURATION STREQUAL "")
        set(HORIZON_DEV_CONFIGURATION "RelWithDebInfo" CACHE STRING "Horizon developer configuration")
    endif()
    if(NOT DEFINED HORIZON_DEV_TARGET_FAMILY OR HORIZON_DEV_TARGET_FAMILY STREQUAL "")
        set(HORIZON_DEV_TARGET_FAMILY "examples" CACHE STRING "Horizon developer target family")
    endif()

    set_property(CACHE HORIZON_DEV_TARGET_FAMILY PROPERTY STRINGS
        core tools examples ocarina ocarina-tests vision-hotfix)
    set(_horizon_target_families core tools examples ocarina ocarina-tests vision-hotfix)
    list(FIND _horizon_target_families "${HORIZON_DEV_TARGET_FAMILY}" _horizon_target_family_index)
    if(_horizon_target_family_index EQUAL -1)
        message(FATAL_ERROR
            "Unsupported HORIZON_DEV_TARGET_FAMILY='${HORIZON_DEV_TARGET_FAMILY}'. "
            "Expected one of: ${_horizon_target_families}")
    endif()

    string(TOLOWER "${HORIZON_DEV_CONFIGURATION}" _horizon_configuration_slug)
    set(_horizon_toolchain
        "${CMAKE_CURRENT_SOURCE_DIR}/build/conan/${HORIZON_DEV_TARGET_FAMILY}/${_horizon_configuration_slug}/generators/conan_toolchain.cmake")
    set(_horizon_build_environment
        "${CMAKE_CURRENT_SOURCE_DIR}/build/conan/${HORIZON_DEV_TARGET_FAMILY}/${_horizon_configuration_slug}/generators/dev_build_environment.cmake")

    if(NOT DEFINED ENV{CORONA_DEV_BOOTSTRAP_ACTIVE})
        find_program(_horizon_conda NAMES conda REQUIRED)
        execute_process(
            COMMAND "${_horizon_conda}" run --name "${HORIZON_CONDA_ENV}" --no-capture-output
                    python tools/dev.py _bootstrap
                    --configuration "${HORIZON_DEV_CONFIGURATION}"
                    --target-family "${HORIZON_DEV_TARGET_FAMILY}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _horizon_bootstrap_result
            OUTPUT_VARIABLE _horizon_bootstrap_stdout
            ERROR_VARIABLE _horizon_bootstrap_stderr
        )
        if(NOT _horizon_bootstrap_result EQUAL 0)
            message(FATAL_ERROR
                "Horizon dependency bootstrap failed (${_horizon_bootstrap_result}).\n"
                "Ensure the '${HORIZON_CONDA_ENV}' Conda environment exists: "
                "conda create --yes --name ${HORIZON_CONDA_ENV} --override-channels --channel conda-forge \"python>=3.11\" \"conan>=2.28,<3\".\n"
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
