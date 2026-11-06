include_guard(GLOBAL)

function(horizon_generate_hotfix_manifest target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Hotfix module target does not exist: ${target}")
    endif()
    get_target_property(target_type "${target}" TYPE)
    get_target_property(imported "${target}" IMPORTED)
    get_target_property(alias "${target}" ALIASED_TARGET)
    if(imported OR alias OR NOT target_type MATCHES "^(MODULE|SHARED)_LIBRARY$")
        message(FATAL_ERROR "Hotfix target must be a buildable module or shared library: ${target}")
    endif()
    if(NOT target MATCHES "^[A-Za-z0-9_+][A-Za-z0-9_.+-]*$" OR target MATCHES "[.]$")
        message(FATAL_ERROR "Unsupported hotfix target name: ${target}")
    endif()

    set(manifest_directory "${CMAKE_BINARY_DIR}/hotfix-manifests/${target}")
    get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(multi_config)
        string(REPLACE ";" "\n" configurations "${CMAKE_CONFIGURATION_TYPES}")
    else()
        set(configurations "$<CONFIG>")
    endif()
    file(GENERATE
        OUTPUT "${manifest_directory}/configurations.txt"
        CONTENT "${configurations}\n"
        NEWLINE_STYLE LF)
    file(GENERATE
        OUTPUT "${manifest_directory}/$<CONFIG>/module.txt"
        CONTENT "$<TARGET_FILE:${target}>\n"
        NEWLINE_STYLE LF)
endfunction()
