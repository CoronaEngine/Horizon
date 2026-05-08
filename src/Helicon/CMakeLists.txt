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

target_link_directories(Helicon PUBLIC
    ${PROJECT_SOURCE_DIR}/third-party/slang/lib
    ${PROJECT_SOURCE_DIR}/third-party/dxc/lib/${TARGET_ARCH}
)

target_link_libraries(Helicon PUBLIC
    glslang
    glslang-default-resource-limits
    spirv-cross-core
    spirv-cross-c
    spirv-cross-cpp
    spirv-cross-util
    slang
    slang-rt
    gfx
    dxcompiler
    dxil
    SPIRV-Tools-link
)

file(GLOB SLANG_DLLS "${PROJECT_SOURCE_DIR}/third-party/slang/bin/*.dll")
file(GLOB DXC_DLLS "${PROJECT_SOURCE_DIR}/third-party/dxc/bin/${TARGET_ARCH}/*.dll")

if(SLANG_DLLS AND DXC_DLLS)
    message(STATUS "Found Slang and DXC DLLs")
    set_target_properties(Helicon PROPERTIES
        INTERFACE_HELICON_RUNTIME_DEPS "${SLANG_DLLS};${DXC_DLLS}"
    )
endif()
