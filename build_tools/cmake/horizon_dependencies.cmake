include_guard(GLOBAL)

# The pinned Conan recipes define these provider targets. Publish stable Horizon
# names globally so the parent engine can consume them after add_subdirectory().
function(_horizon_alias_target alias_name provider)
    _horizon_require_target(${provider} "Conan provider for ${alias_name}")
    if(NOT TARGET ${alias_name})
        add_library(${alias_name} INTERFACE IMPORTED GLOBAL)
        target_link_libraries(${alias_name} INTERFACE ${provider})
    endif()
endfunction()

function(_horizon_require_target target_name detail)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Required Horizon dependency target '${target_name}' is missing: ${detail}")
    endif()
endfunction()
