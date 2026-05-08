cmake_minimum_required(VERSION 4.0)

project(
    Horizon
    VERSION 0.5.0
    HOMEPAGE_URL "https://github.com/CoronaEngine/Horizon"
    LANGUAGES C CXX
)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(HORIZON_BUILD_EXAMPLES "Build Horizon examples (and ocarina tests when ocarina is enabled)" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    NOMINMAX

    $<$<CONFIG:Debug>:CABBAGE_ENGINE_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:CABBAGE_ENGINE_DEBUG>
    $<$<CONFIG:Release>:CABBAGE_ENGINE_RELEASE>
    $<$<CONFIG:MinSizeRel>:CABBAGE_ENGINE_RELEASE>
)

include(FetchContent)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonCoreDependencies.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonRuntimeDeps.cmake)

# ======================== Shader 自动编译模块 ========================
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HeliconShaderCompile.cmake)

add_subdirectory(src)
add_subdirectory(tools)

if(HORIZON_BUILD_EXAMPLES)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonExampleDependencies.cmake)
    add_subdirectory(examples)
endif()

if (MSVC)
    add_compile_options(/Zi)
    add_link_options(/INCREMENTAL:NO)
endif()

find_package(Python)

if(Python_FOUND)
    set(PYTHON_PATH ${Python_EXECUTABLE})
else()
    message(WARNING "Python not found. Please install Python or set the Python_ROOT_DIR to proceed.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonOcarina.cmake)