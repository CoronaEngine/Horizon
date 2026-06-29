include_guard(GLOBAL)

set(HORIZON_DEPENDENCY_PROVIDER "auto" CACHE STRING "Dependency provider: auto, conan, or fetchcontent")
set_property(CACHE HORIZON_DEPENDENCY_PROVIDER PROPERTY STRINGS auto conan fetchcontent)

if(NOT HORIZON_DEPENDENCY_PROVIDER MATCHES "^(auto|conan|fetchcontent)$")
    message(FATAL_ERROR "HORIZON_DEPENDENCY_PROVIDER must be one of: auto, conan, fetchcontent")
endif()

function(_horizon_find_conan_package package_name)
    if(HORIZON_DEPENDENCY_PROVIDER STREQUAL "fetchcontent")
        return()
    endif()

    find_package(${package_name} CONFIG QUIET)
    if(NOT ${package_name}_FOUND AND HORIZON_DEPENDENCY_PROVIDER STREQUAL "conan")
        find_package(${package_name} CONFIG REQUIRED)
    endif()
endfunction()

function(_horizon_alias_target alias_name)
    if(TARGET ${alias_name})
        return()
    endif()

    foreach(candidate IN LISTS ARGN)
        if(TARGET ${candidate})
            add_library(${alias_name} INTERFACE IMPORTED GLOBAL)
            target_link_libraries(${alias_name} INTERFACE ${candidate})
            return()
        endif()
    endforeach()
endfunction()

function(_horizon_header_alias alias_name include_dir)
    if(TARGET ${alias_name})
        return()
    endif()

    string(MAKE_C_IDENTIFIER "${alias_name}" local_name)
    add_library(${local_name} INTERFACE)
    target_include_directories(${local_name} INTERFACE "${include_dir}")
    add_library(${alias_name} ALIAS ${local_name})
endfunction()

function(_horizon_require_target target_name detail)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Required Horizon dependency target '${target_name}' is missing: ${detail}")
    endif()
endfunction()

# Header-only public API dependency. KTM is provided by a local Conan recipe in
# conan/recipes/ktm; FetchContent remains a fallback for existing source caches.
_horizon_find_conan_package(ktm)
_horizon_alias_target(horizon::ktm ktm::ktm)
if(NOT TARGET horizon::ktm)
    horizon_fetchcontent_declare(
        ktm
        EXCLUDE_FROM_ALL
        SOURCE_SUBDIR cmake/horizon-skip-subdir
    )
    FetchContent_MakeAvailable(ktm)
    _horizon_header_alias(horizon::ktm "${ktm_SOURCE_DIR}")
endif()
_horizon_require_target(horizon::ktm "ktm is included by Horizon public headers")

_horizon_find_conan_package(pfr)
_horizon_alias_target(horizon::pfr pfr::pfr pfr)
if(NOT TARGET horizon::pfr)
    horizon_fetchcontent_declare(
        pfr
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(pfr)
    _horizon_header_alias(horizon::pfr "${pfr_SOURCE_DIR}/include")
endif()
_horizon_require_target(horizon::pfr "boost::pfr headers are used by Helicon")

_horizon_find_conan_package(spirv-cross)
_horizon_alias_target(horizon::spirv_cross_core spirv-cross-core spirv-cross::spirv-cross-core)
_horizon_alias_target(horizon::spirv_cross_c spirv-cross-c spirv-cross::spirv-cross-c)
_horizon_alias_target(horizon::spirv_cross_cpp spirv-cross-cpp spirv-cross::spirv-cross-cpp)
_horizon_alias_target(horizon::spirv_cross_glsl spirv-cross-glsl spirv-cross::spirv-cross-glsl)
_horizon_alias_target(horizon::spirv_cross_hlsl spirv-cross-hlsl spirv-cross::spirv-cross-hlsl)
_horizon_alias_target(horizon::spirv_cross_util spirv-cross-util spirv-cross::spirv-cross-util)
if(NOT TARGET horizon::spirv_cross_core)
    horizon_fetchcontent_declare(
        SPIRV-Cross
        EXCLUDE_FROM_ALL
    )
    set(SPIRV_CROSS_SHARED OFF)
    set(SPIRV_CROSS_STATIC ON)
    set(SPIRV_CROSS_ENABLE_TESTS OFF)
    set(SPIRV_CROSS_CLI ${HORIZON_BUILD_DEPENDENCY_TOOLS} CACHE BOOL "" FORCE)
    if(HORIZON_ENABLE_DEPENDENCY_INSTALL)
        set(SPIRV_CROSS_SKIP_INSTALL OFF CACHE BOOL "" FORCE)
    else()
        set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(SPIRV-Cross)
    _horizon_alias_target(horizon::spirv_cross_core spirv-cross-core)
    _horizon_alias_target(horizon::spirv_cross_c spirv-cross-c)
    _horizon_alias_target(horizon::spirv_cross_cpp spirv-cross-cpp)
    _horizon_alias_target(horizon::spirv_cross_glsl spirv-cross-glsl)
    _horizon_alias_target(horizon::spirv_cross_hlsl spirv-cross-hlsl)
    _horizon_alias_target(horizon::spirv_cross_util spirv-cross-util)
endif()
_horizon_require_target(horizon::spirv_cross_core "SPIRV-Cross core target is required by Helicon")
_horizon_require_target(horizon::spirv_cross_c "SPIRV-Cross C target is required by Helicon")
_horizon_require_target(horizon::spirv_cross_cpp "SPIRV-Cross C++ target is required by Helicon")
_horizon_require_target(horizon::spirv_cross_util "SPIRV-Cross util target is required by Helicon")

_horizon_find_conan_package(SPIRV-Tools)
_horizon_alias_target(horizon::spirv_tools_link SPIRV-Tools-link)
if(NOT TARGET horizon::spirv_tools_link)
    horizon_fetchcontent_declare(
        SPIRV-Headers
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SPIRV-Headers)

    horizon_fetchcontent_declare(
        SPIRV-Tools
        EXCLUDE_FROM_ALL
    )
    if(HORIZON_BUILD_DEPENDENCY_TOOLS)
        set(SPIRV_SKIP_EXECUTABLES OFF CACHE BOOL "" FORCE)
    else()
        set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    endif()
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    if(HORIZON_ENABLE_DEPENDENCY_INSTALL)
        set(SKIP_SPIRV_TOOLS_INSTALL OFF CACHE BOOL "" FORCE)
    else()
        set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(SPIRV-Tools)
    _horizon_alias_target(horizon::spirv_tools_link SPIRV-Tools-link)
endif()
_horizon_require_target(horizon::spirv_tools_link "SPIRV-Tools link target is required by Helicon")

if(MSVC AND TARGET SPIRV-Tools-opt)
    get_target_property(_horizon_spirv_tools_opt_imported SPIRV-Tools-opt IMPORTED)
endif()
if(MSVC AND TARGET SPIRV-Tools-opt AND NOT _horizon_spirv_tools_opt_imported)
    target_compile_options(SPIRV-Tools-opt PRIVATE /Wv:18)
    target_compile_options(SPIRV-Tools-opt PRIVATE /wd4717 /wd5232)
endif()
unset(_horizon_spirv_tools_opt_imported)

_horizon_find_conan_package(VulkanHeaders)
_horizon_alias_target(horizon::vulkan_headers vulkan-headers::vulkan-headers Vulkan-Headers)
if(NOT TARGET horizon::vulkan_headers)
    horizon_fetchcontent_declare(
        Vulkan-Headers
        EXCLUDE_FROM_ALL
    )
    set(VULKAN_HEADERS_ENABLE_INSTALL ${HORIZON_ENABLE_DEPENDENCY_INSTALL} CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Vulkan-Headers)
    _horizon_alias_target(horizon::vulkan_headers Vulkan-Headers)
endif()
_horizon_require_target(horizon::vulkan_headers "Vulkan headers are required by Horizon and Volk")

_horizon_find_conan_package(volk)
_horizon_alias_target(horizon::volk volk::volk volk)
if(NOT TARGET horizon::volk)
    set(VOLK_PULL_IN_VULKAN OFF)
    set(VOLK_INSTALL ${HORIZON_ENABLE_DEPENDENCY_INSTALL} CACHE BOOL "" FORCE)
    horizon_fetchcontent_declare(
        volk
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(volk)
    if(TARGET volk)
        target_link_libraries(volk PUBLIC horizon::vulkan_headers)
    endif()
    _horizon_alias_target(horizon::volk volk)
endif()
_horizon_require_target(horizon::volk "Volk is required by the Vulkan backend")

_horizon_find_conan_package(VulkanMemoryAllocator)
_horizon_alias_target(horizon::vma GPUOpen::VulkanMemoryAllocator vulkan-memory-allocator::vulkan-memory-allocator VulkanMemoryAllocator)
if(NOT TARGET horizon::vma)
    horizon_fetchcontent_declare(
        VulkanMemoryAllocator
        EXCLUDE_FROM_ALL
    )
    set(VMA_ENABLE_INSTALL ${HORIZON_ENABLE_DEPENDENCY_INSTALL} CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
    _horizon_alias_target(horizon::vma VulkanMemoryAllocator)
endif()
_horizon_require_target(horizon::vma "Vulkan Memory Allocator is required by the Vulkan backend")

if(HORIZON_BUILD_OCARINA AND DEFINED ENV{CUDA_PATH})
    _horizon_find_conan_package(fmt)
    _horizon_find_conan_package(spdlog)
    _horizon_find_conan_package(xxHash)
    _horizon_alias_target(horizon::fmt fmt::fmt-header-only fmt::fmt)
    _horizon_alias_target(horizon::spdlog spdlog::spdlog spdlog)
    _horizon_alias_target(horizon::xxhash xxHash::xxhash xxhash::xxhash xxhash)

    if(NOT TARGET horizon::fmt)
        set(FMT_OS OFF CACHE BOOL "" FORCE)
        horizon_fetchcontent_declare(
            fmt
            EXCLUDE_FROM_ALL
        )
        FetchContent_MakeAvailable(fmt)
        target_compile_definitions(fmt-header-only INTERFACE
            FMT_EXCEPTIONS=0
            FMT_HEADER_ONLY=1
            FMT_USE_NOEXCEPT=1)
        target_include_directories(fmt-header-only INTERFACE ${fmt_SOURCE_DIR}/include)
        _horizon_alias_target(horizon::fmt fmt::fmt-header-only)
    endif()

    if(NOT TARGET horizon::spdlog)
        set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
        horizon_fetchcontent_declare(
            spdlog
            EXCLUDE_FROM_ALL
        )
        FetchContent_MakeAvailable(spdlog)
        set_target_properties(spdlog PROPERTIES UNITY_BUILD FALSE)
        target_link_libraries(spdlog PUBLIC horizon::fmt)
        _horizon_alias_target(horizon::spdlog spdlog)
    endif()

    if(NOT TARGET horizon::xxhash)
        horizon_fetchcontent_declare(
            xxhash
            EXCLUDE_FROM_ALL
        )
        FetchContent_MakeAvailable(xxhash)
        if(NOT TARGET xxhash)
            add_library(xxhash INTERFACE)
        endif()
        target_include_directories(xxhash INTERFACE ${xxhash_SOURCE_DIR})
        target_compile_definitions(xxhash INTERFACE XXH_INLINE_ALL)
        set_target_properties(xxhash PROPERTIES UNITY_BUILD OFF)
        _horizon_alias_target(horizon::xxhash xxhash)
    endif()
endif()
