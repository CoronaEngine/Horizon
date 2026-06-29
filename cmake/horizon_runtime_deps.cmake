include_guard(GLOBAL)

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