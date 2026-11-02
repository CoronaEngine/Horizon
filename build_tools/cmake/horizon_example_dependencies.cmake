include_guard(GLOBAL)

find_package(glfw3 CONFIG REQUIRED)
_horizon_alias_target(horizon::glfw glfw)

find_package(tinyobjloader CONFIG REQUIRED)
_horizon_alias_target(horizon::tinyobjloader tinyobjloader::tinyobjloader)

find_package(glm CONFIG REQUIRED)
_horizon_alias_target(horizon::glm glm::glm)

find_package(imgui CONFIG REQUIRED)
_horizon_alias_target(horizon::imgui imgui::imgui)

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
