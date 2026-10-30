include_guard(GLOBAL)

find_package(quill CONFIG REQUIRED)
_horizon_require_target(quill::quill "Quill is required by Horizon logging")

find_package(fmt CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
_horizon_alias_target(horizon::fmt fmt::fmt)
_horizon_alias_target(horizon::xxhash xxHash::xxhash)

if(HORIZON_BUILD_OCARINA)
    find_package(spdlog CONFIG REQUIRED)
    _horizon_alias_target(horizon::spdlog spdlog::spdlog)
endif()
