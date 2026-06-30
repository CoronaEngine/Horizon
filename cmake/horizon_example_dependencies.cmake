include_guard(GLOBAL)

function(_horizon_example_alias_target alias_name)
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

function(_horizon_example_require_target target_name detail)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Required Horizon example dependency target '${target_name}' is missing: ${detail}")
    endif()
endfunction()

find_package(stb CONFIG REQUIRED)
_horizon_example_alias_target(horizon::stb stb::stb stb)
_horizon_example_require_target(horizon::stb "stb is required by HorizonExamples")

find_package(glfw3 CONFIG REQUIRED)
_horizon_example_alias_target(horizon::glfw glfw glfw::glfw glfw3)
_horizon_example_require_target(horizon::glfw "glfw is required by HorizonExamples")

find_package(tinyobjloader CONFIG REQUIRED)
_horizon_example_alias_target(horizon::tinyobjloader tinyobjloader::tinyobjloader tinyobjloader)
_horizon_example_require_target(horizon::tinyobjloader "tinyobjloader is required by HorizonExamples")

find_package(glm CONFIG REQUIRED)
_horizon_example_alias_target(horizon::glm glm::glm glm)
_horizon_example_require_target(horizon::glm "glm is required by HorizonExamples")
