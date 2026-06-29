file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

if(NOT HORIZON_ENABLE_HARDCODE_SHADERS)
    list(FILTER SOURCE_FILES EXCLUDE REGEX [[.*[/\\]Compiler[/\\]HardcodeShaders[/\\]HardcodeShaders[^/\\]*\.(cpp|h)$]])
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_FILES})
add_library(Helicon STATIC ${SOURCE_FILES})

target_compile_definitions(Helicon PUBLIC
    HELICON_ROOT_PATH="${PROJECT_SOURCE_DIR}"
)

if(HORIZON_ENABLE_HARDCODE_SHADERS)
    target_compile_definitions(Helicon PUBLIC HELICON_ENABLE_HARDCODE_SHADERS)
endif()

target_include_directories(Helicon PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
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

target_link_libraries(Helicon PUBLIC
    horizon::ktm
    horizon::pfr
    horizon::spirv_cross_core
    horizon::spirv_cross_c
    horizon::spirv_cross_cpp
    horizon::spirv_cross_util
    horizon::slang
    horizon::slang_rt
    horizon::gfx
    horizon::spirv_tools_link
)

set(HELICON_RUNTIME_DEPS)

if(WIN32)
    set(SLANG_DLLS)
    foreach(_horizon_slang_binary_dir IN LISTS HORIZON_SLANG_BINARY_DIRS)
        file(GLOB _horizon_slang_dir_dlls CONFIGURE_DEPENDS "${_horizon_slang_binary_dir}/*.dll")
        list(APPEND SLANG_DLLS ${_horizon_slang_dir_dlls})
    endforeach()

    if(NOT SLANG_DLLS)
        message(FATAL_ERROR "Slang runtime DLLs were not found in Conan package bin directories: ${HORIZON_SLANG_BINARY_DIRS}")
    endif()

    list(REMOVE_DUPLICATES SLANG_DLLS)
    list(APPEND HELICON_RUNTIME_DEPS ${SLANG_DLLS})

    file(GLOB DXC_DLLS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/third-party/dxc/bin/${TARGET_ARCH}/*.dll")
    if(DXC_DLLS)
        list(APPEND HELICON_RUNTIME_DEPS ${DXC_DLLS})
    else()
        message(STATUS "DXC runtime DLLs were not found for ${TARGET_ARCH}; continuing with Slang runtime DLLs only")
    endif()
endif()

if(HELICON_RUNTIME_DEPS)
    message(STATUS "Found Helicon runtime DLLs: ${HELICON_RUNTIME_DEPS}")
    set_target_properties(Helicon PROPERTIES
        INTERFACE_HELICON_RUNTIME_DEPS "${HELICON_RUNTIME_DEPS}"
    )
endif()
