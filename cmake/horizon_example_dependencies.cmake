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

find_package(imgui CONFIG REQUIRED)
_horizon_example_alias_target(horizon::imgui imgui::imgui)
_horizon_example_require_target(horizon::imgui "imgui is required by HorizonExamples")

if(NOT DEFINED HORIZON_IMGUI_BINDINGS_DIR OR HORIZON_IMGUI_BINDINGS_DIR STREQUAL "")
    message(FATAL_ERROR "HORIZON_IMGUI_BINDINGS_DIR must be supplied by the Conan toolchain")
endif()

set(_horizon_imgui_glfw_source "${HORIZON_IMGUI_BINDINGS_DIR}/imgui_impl_glfw.cpp")
if(NOT EXISTS "${_horizon_imgui_glfw_source}")
    message(FATAL_ERROR "Conan imgui package does not provide ${_horizon_imgui_glfw_source}")
endif()

add_library(horizon_imgui STATIC "${_horizon_imgui_glfw_source}")
target_include_directories(horizon_imgui PUBLIC "${HORIZON_IMGUI_BINDINGS_DIR}")
target_link_libraries(horizon_imgui PUBLIC horizon::imgui horizon::glfw)
