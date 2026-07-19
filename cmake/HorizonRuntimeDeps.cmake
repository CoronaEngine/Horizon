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

function(helicon_install_runtime_deps target_name)
    get_target_property(HELICON_DEPS Helicon INTERFACE_HELICON_RUNTIME_DEPS)

    if(NOT HELICON_DEPS)
        message(WARNING "Helicon library did not specify any runtime dependencies.")
        return()
    endif()

    message(STATUS "Scheduling runtime dependencies for ${target_name}: ${HELICON_DEPS}")

    set(DESTINATION_DIR "$<TARGET_FILE_DIR:${target_name}>")

    add_custom_command(
        TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HELICON_DEPS}
        "${DESTINATION_DIR}"
        COMMENT "Copying Helicon runtime dependencies to ${target_name} output directory"
        VERBATIM
    )
endfunction()
