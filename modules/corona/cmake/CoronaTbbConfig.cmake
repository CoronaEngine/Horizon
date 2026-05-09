include_guard(GLOBAL)

if(WIN32)
    set(_plat "win")
else()
    set(_plat "unsupported")
endif()

get_filename_component(
    _corona_default_tbb_dir
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/${_plat}/oneapi-tbb-2022.3.0/lib/cmake/tbb"
    ABSOLUTE)

if(NOT DEFINED TBB_DIR
   OR TBB_DIR STREQUAL ""
   OR NOT EXISTS "${TBB_DIR}/TBBConfig.cmake"
   OR TBB_DIR MATCHES [[/build/_deps/coronaframework-src/]])
    set(TBB_DIR "${_corona_default_tbb_dir}" CACHE PATH "Path to TBB cmake configuration directory" FORCE)
endif()

unset(_corona_default_tbb_dir)
unset(_plat)

find_package(TBB REQUIRED)

function(_corona_collect_files_with_ext _out_var _extension)
    set(_result "")

    foreach(_dir IN LISTS ARGN)
        if(EXISTS "${_dir}")
            file(GLOB _glb CONFIGURE_DEPENDS "${_dir}/*.${_extension}")
            list(APPEND _result ${_glb})
        endif()
    endforeach()

    if(_result)
        list(REMOVE_DUPLICATES _result)
    endif()

    set(${_out_var} "${_result}" PARENT_SCOPE)
endfunction()

function(_corona_collect_tbb_redist_artifacts)
    if(NOT TBB_FOUND)
        return()
    endif()

    get_filename_component(_tbb_config_dir "${TBB_DIR}" ABSOLUTE)
    get_filename_component(_tbb_root "${_tbb_config_dir}/../../.." ABSOLUTE)

    if(CMAKE_SIZEOF_VOID_P STREQUAL "8")
        set(_tbb_intel_arch intel64)
        set(_tbb_arch_suffix "")
    else()
        set(_tbb_intel_arch ia32)
        set(_tbb_arch_suffix 32)
    endif()

    set(_tbb_subdir vc14)

    if(DEFINED WINDOWS_STORE AND WINDOWS_STORE)
        set(_tbb_subdir "${_tbb_subdir}_uwp")
    endif()

    set(_candidate_suffixes
        "redist/${_tbb_intel_arch}/${_tbb_subdir}"
        "bin${_tbb_arch_suffix}/${_tbb_subdir}"
        "bin${_tbb_arch_suffix}"
        "bin"
    )

    set(_candidate_dirs "")

    foreach(_suffix IN LISTS _candidate_suffixes)
        get_filename_component(_dir "${_tbb_root}/${_suffix}" ABSOLUTE)
        list(APPEND _candidate_dirs "${_dir}")
    endforeach()

    _corona_collect_files_with_ext(CORONA_TBB_REDIS_DLLS dll ${_candidate_dirs})
    _corona_collect_files_with_ext(CORONA_TBB_REDIS_PDBS pdb ${_candidate_dirs})

    set(CORONA_TBB_REDIS_DLLS "${CORONA_TBB_REDIS_DLLS}" CACHE STRING "Collected TBB redist DLLs" FORCE)
    set(CORONA_TBB_REDIS_PDBS "${CORONA_TBB_REDIS_PDBS}" CACHE STRING "Collected TBB redist PDBs" FORCE)
    mark_as_advanced(CORONA_TBB_REDIS_DLLS CORONA_TBB_REDIS_PDBS)
endfunction()

function(corona_copy_tbb_runtime_artifacts target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "corona_copy_tbb_runtime_artifacts: target '${target_name}' does not exist")
    endif()

    if(ARGC GREATER 1 AND NOT "${ARGV1}" STREQUAL "")
        set(_destination "${ARGV1}")
    else()
        set(_destination "$<TARGET_FILE_DIR:${target_name}>")
    endif()

    if(NOT CORONA_TBB_REDIS_DLLS AND NOT CORONA_TBB_REDIS_PDBS AND NOT CORONA_TBB_REDIS_DEFS)
        message(WARNING "No TBB runtime artifacts were collected; corona_copy_tbb_runtime_artifacts skipped")
        return()
    endif()

    foreach(_artifact IN LISTS CORONA_TBB_REDIS_DLLS CORONA_TBB_REDIS_PDBS CORONA_TBB_REDIS_DEFS)
        if(NOT EXISTS "${_artifact}")
            message(WARNING "Missing TBB artifact: ${_artifact}")
            continue()
        endif()

        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_destination}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_artifact}" "${_destination}"
            COMMENT "Copying ${_artifact} to runtime directory for ${target_name}"
        )
    endforeach()
endfunction()

if(TBB_FOUND)
    message(STATUS "TBB found: ${TBB_VERSION}")
    message(STATUS "TBB import targets: ${TBB_IMPORTED_TARGETS}")
    _corona_collect_tbb_redist_artifacts()
else()
    message(FATAL_ERROR "TBB not found")
endif()