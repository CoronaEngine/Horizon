file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_FILES})
add_library(Helicon STATIC ${SOURCE_FILES})

target_compile_definitions(Helicon PUBLIC
    HELICON_ROOT_PATH="${PROJECT_SOURCE_DIR}"
)

target_include_directories(Helicon PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ktm_SOURCE_DIR}"
    "${pfr_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/third-party/slang/include"
    "${PROJECT_SOURCE_DIR}/third-party/dxc/inc"
)

# --- 架构检测 ---
if(MSVC)
    message(STATUS "MSVC detected")

    if(DEFINED CMAKE_VS_PLATFORM_NAME)
        set(TARGET_ARCH ${CMAKE_VS_PLATFORM_NAME})
    else()
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(TARGET_ARCH "x64")
        else()
            set(TARGET_ARCH "x86")
        endif()
    endif()

    if(TARGET_ARCH STREQUAL "Win32")
        set(TARGET_ARCH "x86")
    endif()
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" TARGET_ARCH)

    if(TARGET_ARCH STREQUAL "amd64" OR TARGET_ARCH STREQUAL "x86_64")
        set(TARGET_ARCH "x64")
    elseif(TARGET_ARCH STREQUAL "aarch64")
        set(TARGET_ARCH "arm64")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4 AND(TARGET_ARCH STREQUAL "x86" OR TARGET_ARCH STREQUAL "i386"))
        set(TARGET_ARCH "x86")
    endif()
endif()

message(STATUS "Target Arch: ${TARGET_ARCH}")

find_package(slang CONFIG QUIET)

if(TARGET slang::slang)
    set(HELICON_SLANG_TARGET slang::slang)
else()
    find_library(SLANG_LIBRARY
        NAMES slang
        HINTS "${PROJECT_SOURCE_DIR}/third-party/slang/lib"
    )

    if(NOT SLANG_LIBRARY)
        message(FATAL_ERROR
            "Slang library not found for this platform. "
            "Install shader-slang with vcpkg/CMake package support, or copy a native Slang library into third-party/slang/lib. "
            "Current bundled Slang binaries appear to be Windows-only."
        )
    endif()

    set(HELICON_SLANG_TARGET ${SLANG_LIBRARY})
endif()

target_link_libraries(Helicon PUBLIC
    glslang
    glslang-default-resource-limits
    spirv-cross-core
    spirv-cross-c
    spirv-cross-cpp
    spirv-cross-util
    ${HELICON_SLANG_TARGET}
    SPIRV-Tools-link
)

if(WIN32)
    find_library(SLANG_RT_LIBRARY
        NAMES slang-rt
        PATHS "${PROJECT_SOURCE_DIR}/third-party/slang/lib"
        NO_DEFAULT_PATH
        REQUIRED
    )
    find_library(GFX_LIBRARY
        NAMES gfx
        PATHS "${PROJECT_SOURCE_DIR}/third-party/slang/lib"
        NO_DEFAULT_PATH
        REQUIRED
    )
    find_library(DXCOMPILER_LIBRARY
        NAMES dxcompiler
        PATHS "${PROJECT_SOURCE_DIR}/third-party/dxc/lib/${TARGET_ARCH}"
        NO_DEFAULT_PATH
        REQUIRED
    )
    find_library(DXIL_LIBRARY
        NAMES dxil
        PATHS "${PROJECT_SOURCE_DIR}/third-party/dxc/lib/${TARGET_ARCH}"
        NO_DEFAULT_PATH
        REQUIRED
    )

    target_link_libraries(Helicon PUBLIC
        ${SLANG_RT_LIBRARY}
        ${GFX_LIBRARY}
        ${DXCOMPILER_LIBRARY}
        ${DXIL_LIBRARY}
    )
endif()

file(GLOB SLANG_RUNTIME_DEPS
    "${PROJECT_SOURCE_DIR}/third-party/slang/bin/*.dll"
    "${PROJECT_SOURCE_DIR}/third-party/slang/bin/*.dylib"
    "${PROJECT_SOURCE_DIR}/third-party/slang/lib/*.dylib"
)

if(WIN32)
    file(GLOB DXC_RUNTIME_DEPS
        "${PROJECT_SOURCE_DIR}/third-party/dxc/bin/${TARGET_ARCH}/*.dll"
    )
else()
    set(DXC_RUNTIME_DEPS)
endif()

if(SLANG_RUNTIME_DEPS)
    message(STATUS "Found Helicon runtime dependencies")
    set_target_properties(Helicon PROPERTIES
        INTERFACE_HELICON_RUNTIME_DEPS "${SLANG_RUNTIME_DEPS};${DXC_RUNTIME_DEPS}"
    )
endif()
