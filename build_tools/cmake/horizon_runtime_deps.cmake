include_guard(GLOBAL)

function(horizon_install_runtime_deps target_name provider_target)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "horizon_install_runtime_deps: target '${target_name}' does not exist")
    endif()

    add_custom_command(
        TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target_name}>
            $<TARGET_GENEX_EVAL:${provider_target},$<TARGET_PROPERTY:${provider_target},INTERFACE_HORIZON_RUNTIME_DEPS>>
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime dependencies from ${provider_target} to ${target_name}"
        VERBATIM
    )
endfunction()

function(horizon_copy_runtime_files target_name)
    if(NOT WIN32 OR NOT ARGN)
        return()
    endif()
    set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS "${ARGN}")
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ARGN} "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND_EXPAND_LISTS VERBATIM
        COMMENT "Copying Conan runtime files to ${target_name}")
endfunction()

function(horizon_install_slang_runtime target_name)
    get_property(_runtime GLOBAL PROPERTY HORIZON_SLANG_RUNTIME_FILES)
    horizon_copy_runtime_files(${target_name} ${_runtime})
endfunction()

function(helicon_install_runtime_deps target_name)
    horizon_install_vulkan_runtime(${target_name})
    horizon_install_slang_runtime(${target_name})
endfunction()
