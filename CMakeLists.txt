# Public CMake entry point. Implementation lives in build_tools/.
cmake_minimum_required(VERSION 4.0)

include("${CMAKE_CURRENT_LIST_DIR}/build_tools/cmake/horizon_paths.cmake")
list(APPEND CMAKE_MODULE_PATH "${HORIZON_BUILD_TOOLS_DIR}/cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(horizon_dev_bootstrap)
    horizon_dev_bootstrap()
endif()

project(Horizon VERSION 0.5.0
    HOMEPAGE_URL "https://github.com/CoronaEngine/Horizon"
    LANGUAGES C CXX)

include(horizon_project)
