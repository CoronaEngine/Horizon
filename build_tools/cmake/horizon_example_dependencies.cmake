include_guard(GLOBAL)

find_package(glfw3 CONFIG REQUIRED)
_horizon_alias_target(horizon::glfw glfw)

find_package(tinyobjloader CONFIG REQUIRED)
_horizon_alias_target(horizon::tinyobjloader tinyobjloader::tinyobjloader)

find_package(glm CONFIG REQUIRED)
_horizon_alias_target(horizon::glm glm::glm)
