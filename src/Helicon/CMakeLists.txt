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

# Expose the collected Slang files to parent-project runtime/test helpers.
set_property(TARGET Helicon PROPERTY
    INTERFACE_HELICON_RUNTIME_DEPS "${HORIZON_SLANG_RUNTIME_FILES}")
