
include(FetchContent)

add_library(ocarina-ext SHARED empty.cpp)
set_target_properties(ocarina-ext PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

# ---- xxhash / fmt / spdlog 由根 Horizon CMakeLists.txt 通过 FetchContent 提供，
#      并由根级直接 target_link_libraries 到 ocarina-ext，本文件不再处理。

add_subdirectory(EASTL)
target_include_directories(EASTL PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/EASTL/include)
target_link_libraries(ocarina-ext PUBLIC EASTL)


add_subdirectory(tinyexr)
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
        ocarina-ext-tinyexr)