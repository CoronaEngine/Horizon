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

target_link_libraries(Helicon PUBLIC
    horizon::ktm
    horizon::pfr
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
endif()

if(HELICON_RUNTIME_DEPS)
    message(STATUS "Found Helicon runtime DLLs: ${HELICON_RUNTIME_DEPS}")
    set_target_properties(Helicon PROPERTIES
        INTERFACE_HELICON_RUNTIME_DEPS "${HELICON_RUNTIME_DEPS}"
    )
endif()
