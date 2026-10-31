include_guard(GLOBAL)

find_package(stb CONFIG REQUIRED)
_horizon_alias_target(horizon::stb stb::stb)

find_package(tinyexr CONFIG REQUIRED)
_horizon_alias_target(horizon::tinyexr tinyexr::tinyexr)
