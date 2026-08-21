cmake_minimum_required(VERSION 4.0)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_dev_bootstrap.cmake)
    horizon_dev_bootstrap()
endif()

project(
    Horizon
    VERSION 0.5.0
    HOMEPAGE_URL "https://github.com/CoronaEngine/Horizon"
    LANGUAGES C CXX
)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(HORIZON_BUILD_TOOLS "Build Horizon tool executables" OFF)
option(HORIZON_BUILD_EXAMPLES "Build Horizon examples (and ocarina tests when ocarina is enabled)" ${PROJECT_IS_TOP_LEVEL})
option(HORIZON_BUILD_TESTS "Build Horizon correctness tests" ON)
option(HORIZON_BUILD_BENCHMARKS "Build Horizon performance benchmarks" OFF)
option(HORIZON_BUILD_OCARINA "Build the bundled ocarina module when CUDA is available" OFF)
option(HORIZON_BUILD_OCARINA_TESTS "Build bundled ocarina test executables" OFF)
option(HORIZON_BUILD_VISION_HOTFIX "Build Vision hotfix support targets" OFF)
option(HORIZON_ENABLE_HARDCODE_SHADERS "Compile and use generated Helicon hardcoded shader cache" OFF)
option(HORIZON_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)
option(HORIZON_ENABLE_IMGUI_RENDER "Render the ImGui overlay in Horizon examples" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_TLS_VERIFY OFF)

add_compile_definitions(
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    NOMINMAX

    $<$<CONFIG:Debug>:CABBAGE_ENGINE_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:CABBAGE_ENGINE_DEBUG>
    $<$<CONFIG:Release>:CABBAGE_ENGINE_RELEASE>
    $<$<CONFIG:MinSizeRel>:CABBAGE_ENGINE_RELEASE>
)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_slang.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_core_dependencies.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_tracy.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_runtime_deps.cmake)

# ======================== Shader 自动编译模块 ========================
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HeliconShaderCompile.cmake)

add_subdirectory(src)

if(HORIZON_BUILD_TOOLS OR HORIZON_BUILD_EXAMPLES)
    add_subdirectory(tools)
endif()

if(HORIZON_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_testing.cmake)
    add_subdirectory(tests)
endif()

if(HORIZON_BUILD_BENCHMARKS)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_benchmarking.cmake)
    add_subdirectory(benchmarks)
endif()

if(HORIZON_BUILD_EXAMPLES)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_example_dependencies.cmake)
    add_subdirectory(examples)
endif()

if (MSVC)
    add_compile_options(/Zi)
    add_link_options(/INCREMENTAL:NO)

    target_compile_options(Helicon PUBLIC /utf-8)
endif()

find_package(Python)

if(Python_FOUND)
    set(PYTHON_PATH ${Python_EXECUTABLE})
else()
    message(WARNING "Python not found. Please install Python or set the Python_ROOT_DIR to proceed.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_ocarina.cmake)

if(HORIZON_BUILD_VISION_HOTFIX)
    if(NOT TARGET ocarina OR NOT TARGET ocarina-include)
        message(FATAL_ERROR "HORIZON_BUILD_VISION_HOTFIX requires HORIZON_BUILD_OCARINA=ON and available ocarina targets")
    endif()
    target_include_directories(ocarina-include INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_subdirectory(src/hotfix)
    target_link_libraries(ocarina-include INTERFACE horizon-hotfix)

    if(TARGET HorizonExamples)
        target_link_libraries(HorizonExamples PRIVATE horizon-hotfix-all)
    endif()
endif()
