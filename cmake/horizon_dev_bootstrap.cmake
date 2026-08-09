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
        # Search for conda in multiple locations:
        # - conda.exe in Scripts/ (works in Git Bash)
        # - conda.bat in condabin/ (works in cmd/PowerShell)
        # - conda in PATH (Unix-like systems)
        find_program(_horizon_conda
            NAMES conda.exe conda.bat conda
            PATHS
                "$ENV{CONDA_PREFIX}/Scripts"
                "$ENV{CONDA_PREFIX}/condabin"
                "$ENV{USERPROFILE}/miniconda3/Scripts"
                "$ENV{USERPROFILE}/miniconda3/condabin"
                "$ENV{USERPROFILE}/anaconda3/Scripts"
                "$ENV{USERPROFILE}/anaconda3/condabin"
                "$ENV{HOME}/miniconda3/Scripts"
                "$ENV{HOME}/miniconda3/condabin"
                "$ENV{HOME}/anaconda3/Scripts"
                "$ENV{HOME}/anaconda3/condabin"
            DOC "Conda package manager executable"
            REQUIRED
        )

        # Check if conda environment exists
        # Note: conda create --yes will REMOVE and recreate existing environments, so we must check first
        execute_process(
            COMMAND "${_horizon_conda}" env list --json
            OUTPUT_VARIABLE _conda_env_list_json
            ERROR_QUIET
        )

        string(JSON _conda_env_count ERROR_VARIABLE _json_error LENGTH "${_conda_env_list_json}" "envs")
        set(_env_exists FALSE)
        if(NOT _json_error)
            math(EXPR _conda_env_last_index "${_conda_env_count} - 1")
            foreach(_env_index RANGE 0 ${_conda_env_last_index})
                string(JSON _env_path GET "${_conda_env_list_json}" "envs" ${_env_index})
                string(FIND "${_env_path}" "${HORIZON_CONDA_ENV}" _env_name_pos)
                if(NOT _env_name_pos EQUAL -1)
                    set(_env_exists TRUE)
                    break()
                endif()
            endforeach()
        endif()

        if(NOT _env_exists)
            message(STATUS "Creating conda environment '${HORIZON_CONDA_ENV}' (this may take a few minutes)...")
            execute_process(
                COMMAND "${_horizon_conda}" create --yes --name "${HORIZON_CONDA_ENV}"
                        --override-channels --channel conda-forge
                        "python>=3.11" "conan>=2.28,<3"
                RESULT_VARIABLE _horizon_env_create_result
                COMMAND_ECHO STDOUT
            )
            if(NOT _horizon_env_create_result EQUAL 0)
                message(FATAL_ERROR
                    "Failed to create conda environment '${HORIZON_CONDA_ENV}' (exit code ${_horizon_env_create_result}).")
            endif()
        else()
            message(STATUS "Using existing conda environment '${HORIZON_CONDA_ENV}'")
        endif()

        message(STATUS "Running Horizon dependency bootstrap...")
        execute_process(
            COMMAND "${_horizon_conda}" run --name "${HORIZON_CONDA_ENV}" --no-capture-output
                    python tools/dev.py _bootstrap
                    --configuration "${HORIZON_DEV_CONFIGURATION}"
                    --target-family "${HORIZON_DEV_TARGET_FAMILY}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _horizon_bootstrap_result
            COMMAND_ECHO STDOUT
        )
        if(NOT _horizon_bootstrap_result EQUAL 0)
            message(FATAL_ERROR
                "Horizon dependency bootstrap failed (exit code ${_horizon_bootstrap_result}).")
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
