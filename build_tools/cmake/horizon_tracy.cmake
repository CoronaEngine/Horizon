include_guard(GLOBAL)

if(NOT HORIZON_ENABLE_TRACY)
    return()
endif()

find_package(Tracy CONFIG REQUIRED)

if(NOT TARGET Tracy::TracyClient)
    message(FATAL_ERROR "Tracy is enabled, but Conan did not provide Tracy::TracyClient")
endif()
