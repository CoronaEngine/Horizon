cmake_minimum_required(VERSION 4.0)

project(
    Horizon
    VERSION 0.5.0
    HOMEPAGE_URL "https://github.com/CoronaEngine/Horizon"
    LANGUAGES C CXX
)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(HORIZON_BUILD_TOOLS "Build Horizon tool executables" OFF)
option(HORIZON_BUILD_EXAMPLES "Build Horizon examples (and ocarina tests when ocarina is enabled)" ${PROJECT_IS_TOP_LEVEL})
option(HORIZON_BUILD_TESTS "Build Horizon correctness tests" OFF)
#option(HORIZON_BUILD_EXAMPLES "Build Horizon examples" OFF)
#option(HORIZON_BUILD_TESTS "Build Horizon correctness tests" OFF)
option(HORIZON_BUILD_BENCHMARKS "Build Horizon performance benchmarks" OFF)
option(HORIZON_BUILD_OCARINA "Build the bundled ocarina module when CUDA is available" OFF)
option(HORIZON_BUILD_OCARINA_TESTS "Build bundled ocarina test executables" OFF)
option(HORIZON_BUILD_DEPENDENCY_TOOLS "Build third-party command line tool executables" OFF)
option(HORIZON_ENABLE_DEPENDENCY_INSTALL "Enable third-party dependency install rules" OFF)
option(HORIZON_ENABLE_VALIDATION "Enable Horizon runtime validation checks" ON)
option(HORIZON_ENABLE_HARDCODE_SHADERS "Compile and use generated Helicon hardcoded shader cache" OFF)

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

include(FetchContent)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonFetchContent.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonSlang.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonCoreDependencies.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonRuntimeDeps.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonCorona.cmake)

# ======================== Shader 自动编译模块 ========================
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HeliconShaderCompile.cmake)

add_subdirectory(src)

if(HORIZON_BUILD_TOOLS OR HORIZON_BUILD_EXAMPLES)
    add_subdirectory(tools)
endif()

if(HORIZON_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonTesting.cmake)
    add_subdirectory(tests)
endif()

if(HORIZON_BUILD_BENCHMARKS)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonBenchmarking.cmake)
    add_subdirectory(benchmarks)
endif()

if(HORIZON_BUILD_EXAMPLES)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonExampleDependencies.cmake)
    add_subdirectory(examples)
endif()

if (MSVC)
    add_compile_options(/Zi)
    add_link_options(/INCREMENTAL:NO)

    target_compile_options(Helicon PUBLIC /source-charset:utf-8 /execution-charset:utf-8)
endif()

find_package(Python)

if(Python_FOUND)
    set(PYTHON_PATH ${Python_EXECUTABLE})
else()
    message(WARNING "Python not found. Please install Python or set the Python_ROOT_DIR to proceed.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HorizonOcarina.cmake)
