cmake_minimum_required(VERSION 4.0)
project(CoronaFramework VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CoronaTbbConfig)
include(CoronaOptions)
include(CoronaRuntime)
include(CoronaThirdParty)

add_subdirectory(src/pal)
add_subdirectory(src/kernel)

if(CORONA_FRAMEWORK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 测试
if(CORONA_FRAMEWORK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
