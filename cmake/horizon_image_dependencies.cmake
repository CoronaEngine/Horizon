include_guard(GLOBAL)

function(_horizon_image_alias_target alias_name)
    if(TARGET ${alias_name})
        return()
    endif()

    foreach(candidate IN LISTS ARGN)
        if(TARGET ${candidate})
            add_library(${alias_name} INTERFACE IMPORTED GLOBAL)
            target_link_libraries(${alias_name} INTERFACE ${candidate})
            return()
        endif()
    endforeach()
endfunction()

function(_horizon_image_require_target target_name detail)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Required Horizon Image dependency target '${target_name}' is missing: ${detail}")
    endif()
endfunction()

find_package(stb CONFIG REQUIRED)
_horizon_image_alias_target(horizon::stb stb::stb stb)
_horizon_image_require_target(horizon::stb "stb is required by horizon-image")

find_package(tinyexr CONFIG REQUIRED)
_horizon_image_alias_target(horizon::tinyexr tinyexr::tinyexr tinyexr)
_horizon_image_require_target(horizon::tinyexr "TinyEXR is required by horizon-image")
