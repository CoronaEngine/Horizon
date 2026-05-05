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

if(MSVC)
    # Read source files as UTF-8 so Chinese comments do not break parsing under legacy code pages.
    add_compile_options(/utf-8)
endif()

add_compile_definitions(
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    NOMINMAX

    $<$<CONFIG:Debug>:CABBAGE_ENGINE_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:CABBAGE_ENGINE_DEBUG>
    $<$<CONFIG:Release>:CABBAGE_ENGINE_RELEASE>
    $<$<CONFIG:MinSizeRel>:CABBAGE_ENGINE_RELEASE>
)

include(FetchContent)

# ======================== Helicon 依赖 ========================
FetchContent_Declare(
    pfr
    GIT_REPOSITORY https://github.com/boostorg/pfr.git
    GIT_TAG develop
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(pfr)

FetchContent_Declare(
    ktm
    GIT_REPOSITORY https://github.com/YGXXD/ktm.git
    GIT_TAG main
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(ktm)

FetchContent_Declare(
    preprocessor
    GIT_REPOSITORY https://github.com/boostorg/preprocessor.git
    GIT_TAG develop
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(preprocessor)

FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG main
    EXCLUDE_FROM_ALL
)
set(ENABLE_OPT OFF)
set(ENABLE_GLSLANG_BINARIES OFF)
FetchContent_MakeAvailable(glslang)

FetchContent_Declare(
    SPIRV-Cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG main
    EXCLUDE_FROM_ALL
)
set(SPIRV_CROSS_SHARED OFF)
set(SPIRV_CROSS_STATIC ON)
set(SPIRV_CROSS_ENABLE_TESTS OFF)
set(SPIRV_CROSS_CLI OFF)
FetchContent_MakeAvailable(SPIRV-Cross)

FetchContent_Declare(
    SPIRV-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
    GIT_TAG vulkan-sdk-1.4.341
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SPIRV-Headers)

FetchContent_Declare(
    SPIRV-Tools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
    GIT_TAG vulkan-sdk-1.4.341
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SPIRV-Tools)

# 修复 SPIRV-Tools 在 C++20 下的运算符重写警告 (C5232)
if(MSVC)
    target_compile_options(SPIRV-Tools-opt PRIVATE /Wv:18)
    target_compile_options(SPIRV-Tools-opt PRIVATE /wd4717 /wd5232)
endif()

# ======================== ocarina 共享依赖（在线拉取，集中管理） ========================
# 这些库当前仅 ocarina 子项目使用；在根级声明以统一 FetchContent 入口。
# EXCLUDE_FROM_ALL 保证未启用 ocarina 时不参与构建。

# ---- fmt (header-only) ----
set(FMT_OS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG main
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(fmt)
target_compile_definitions(fmt-header-only INTERFACE
        FMT_EXCEPTIONS=0
        FMT_HEADER_ONLY=1
        FMT_USE_NOEXCEPT=1)
target_include_directories(fmt-header-only INTERFACE ${fmt_SOURCE_DIR}/include)

# ---- spdlog (强制使用外部 fmt) ----
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.x
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(spdlog)
set_target_properties(spdlog PROPERTIES
        UNITY_BUILD FALSE)
target_link_libraries(spdlog PUBLIC fmt::fmt-header-only)

# ---- xxHash (header-only, 不使用其顶层 CMakeLists) ----
FetchContent_Declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG dev
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(xxhash)
add_library(xxhash INTERFACE)
target_include_directories(xxhash INTERFACE ${xxhash_SOURCE_DIR})
target_compile_definitions(xxhash INTERFACE XXH_INLINE_ALL)
set_target_properties(xxhash PROPERTIES
        UNITY_BUILD OFF)

# ======================== Helicon 运行时 DLL 部署函数 ========================
function(helicon_install_runtime_deps target_name)
    get_target_property(HELICON_DEPS Helicon INTERFACE_HELICON_RUNTIME_DEPS)

    if(NOT HELICON_DEPS)
        message(WARNING "Helicon library did not specify any runtime dependencies.")
        return()
    endif()

    message(STATUS "Scheduling runtime dependencies for ${target_name}: ${HELICON_DEPS}")

    set(DESTINATION_DIR "$<TARGET_FILE_DIR:${target_name}>")

    add_custom_command(
        TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HELICON_DEPS}
        "${DESTINATION_DIR}"
        COMMENT "Copying Helicon runtime dependencies to ${target_name} output directory"
        VERBATIM
    )
endfunction()

# ======================== Shader 自动编译模块 ========================
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HeliconShaderCompile.cmake)

# ======================== 其他依赖 ========================
FetchContent_Declare(CoronaFramework
    GIT_REPOSITORY https://github.com/CoronaEngine/CoronaFramework.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(CoronaFramework)

set(VOLK_PULL_IN_VULKAN OFF) # We will provide Vulkan-Headers ourselves
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(volk)

FetchContent_Declare(
    Vulkan-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.341
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Vulkan-Headers)

FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

add_subdirectory(Src)
add_subdirectory(Scripts)

if(HORIZON_BUILD_EXAMPLES)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(stb)

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG master
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(glfw)

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


if(DEFINED ENV{CUDA_PATH})
    include_directories(ocarina)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")

    set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
    set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)

    add_subdirectory(ocarina)

    # 由根级负责把共享依赖装配到 ocarina-ext 上，子目录不再引用这些库的目标名
    if(TARGET ocarina-ext)
        target_link_libraries(ocarina-ext PUBLIC
            fmt::fmt-header-only
            spdlog
            xxhash)
    endif()
endif()