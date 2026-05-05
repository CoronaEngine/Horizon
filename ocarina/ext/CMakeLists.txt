
include(FetchContent)

add_library(ocarina-ext SHARED empty.cpp)
set_target_properties(ocarina-ext PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

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

target_link_libraries(ocarina-ext PUBLIC fmt::fmt-header-only)

add_subdirectory(EASTL)
target_include_directories(EASTL PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/EASTL/include)
target_link_libraries(ocarina-ext PUBLIC EASTL)


add_subdirectory(tinyexr)

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
target_link_libraries(ocarina-ext PUBLIC spdlog)
add_subdirectory(stblib)

cmake_policy(SET CMP0148 NEW)

# ---- SPIRV-Cross 由根 Horizon CMakeLists.txt 通过 FetchContent 提供 ----
if(VISION_BUILD_VULKAN)
    set_property(TARGET spirv-cross PROPERTY FOLDER "ocarina/ext/spirv")
    set_property(TARGET spirv-cross-c PROPERTY FOLDER "ocarina/ext/spirv")
    set_property(TARGET spirv-cross-core PROPERTY FOLDER "ocarina/ext/spirv")
    set_property(TARGET spirv-cross-glsl PROPERTY FOLDER "ocarina/ext/spirv")
    set_property(TARGET spirv-cross-hlsl PROPERTY FOLDER "ocarina/ext/spirv")
    set_property(TARGET spirv-cross-msl PROPERTY FOLDER "ocarina/ext/spirv")
    set_property(TARGET spirv-cross-cpp PROPERTY FOLDER "ocarina/ext/spirv")
    set_property(TARGET spirv-cross-reflect PROPERTY FOLDER "ocarina/ext/spirv")
    set_property(TARGET spirv-cross-util PROPERTY FOLDER "ocarina/ext/spirv")
endif()
#list(APPEND SPIRV_LIBS "spirv-cross-core" "spirv-cross-hlsl" )

target_link_libraries(ocarina-ext
        PUBLIC
        ocarina-ext-stb
        xxhash
        spdlog
        ocarina-ext-tinyexr)