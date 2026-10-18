include_guard(GLOBAL)

# ======================== Helicon 依赖 ========================
FetchContent_Declare(
    pfr
    GIT_REPOSITORY https://github.com/boostorg/pfr.git
    GIT_TAG 401385c240027423acbb1eb6dea2abe0043db5aa
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(pfr)

FetchContent_Declare(
    ktm
    GIT_REPOSITORY https://github.com/YGXXD/ktm.git
    GIT_TAG 1a9138fdfd7b7bc7f8a092e222545d81cc2bf418
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(ktm)

FetchContent_Declare(
    preprocessor
    GIT_REPOSITORY https://github.com/boostorg/preprocessor.git
    GIT_TAG cd1b1bd03900b68505822cfa25cb16851bd6caf1
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(preprocessor)

FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG a0b513644803358f66ea6379e5d206963835afac
    EXCLUDE_FROM_ALL
)
set(ENABLE_OPT OFF)
set(ENABLE_GLSLANG_BINARIES OFF)
FetchContent_MakeAvailable(glslang)

FetchContent_Declare(
    SPIRV-Cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG 6c09849fe88c48eaed08413aa022aaa136a3a057
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
    GIT_TAG 04f10f650d514df88b76d25e83db360142c7b174
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SPIRV-Headers)

FetchContent_Declare(
    SPIRV-Tools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
    GIT_TAG fbe4f3ad913c44fe8700545f8ffe35d1382b7093
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SPIRV-Tools)

# 修复 SPIRV-Tools 在 C++20 下的运算符重写警告 (C5232)
if(MSVC AND TARGET SPIRV-Tools-opt)
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
    GIT_TAG b5d1e5404bd678df41f20d0f053222a6c60fe74b
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
    GIT_TAG 2ee3cf8204ed5048627644e00a51a7d93fbc4786
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
    GIT_TAG e573d4d2aaeaba0f3e5a0a9a54144a1f2b4b56e7
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(xxhash)
if(NOT TARGET xxhash)
    add_library(xxhash INTERFACE)
endif()
target_include_directories(xxhash INTERFACE ${xxhash_SOURCE_DIR})
target_compile_definitions(xxhash INTERFACE XXH_INLINE_ALL)
set_target_properties(xxhash PROPERTIES
        UNITY_BUILD OFF)

# ======================== 其他依赖 ========================
set(VOLK_PULL_IN_VULKAN OFF) # We will provide Vulkan-Headers ourselves
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 776893306c5d3b22b6185b5d4a258b81d94572bf
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(volk)

FetchContent_Declare(
    Vulkan-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG b5c8f996196ba4aa6d8f97e52b5d3b6e70f7e4e2
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Vulkan-Headers)

FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG 3aa921224c154a0d2c43912bc88e1c42ce1f7607
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)