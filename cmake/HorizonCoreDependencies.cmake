include_guard(GLOBAL)

# ======================== Helicon 依赖 ========================
horizon_fetchcontent_declare(
    pfr
    GIT_REPOSITORY https://github.com/boostorg/pfr.git
    GIT_TAG develop
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(pfr)

horizon_fetchcontent_declare(
    ktm
    GIT_REPOSITORY https://github.com/YGXXD/ktm.git
    GIT_TAG main
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(ktm)

horizon_fetchcontent_declare(
    preprocessor
    GIT_REPOSITORY https://github.com/boostorg/preprocessor.git
    GIT_TAG develop
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(preprocessor)

horizon_fetchcontent_declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG main
    EXCLUDE_FROM_ALL
)
set(ENABLE_OPT OFF)
set(ENABLE_GLSLANG_BINARIES OFF)
FetchContent_MakeAvailable(glslang)

horizon_fetchcontent_declare(
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

horizon_fetchcontent_declare(
    SPIRV-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
    GIT_TAG vulkan-sdk-1.4.341
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SPIRV-Headers)

horizon_fetchcontent_declare(
    SPIRV-Tools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
    GIT_TAG vulkan-sdk-1.4.341
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
horizon_fetchcontent_declare(
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
horizon_fetchcontent_declare(
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
horizon_fetchcontent_declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG dev
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
horizon_fetchcontent_declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(volk)

horizon_fetchcontent_declare(
    Vulkan-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.341
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Vulkan-Headers)

horizon_fetchcontent_declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)
