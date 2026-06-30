add_library(ocarina-ext SHARED empty.cpp)
set_target_properties(ocarina-ext PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

# fmt, spdlog, and xxHash are provided by the root Conan dependency graph and
# linked to ocarina-ext from the Horizon root.

add_subdirectory(EASTL)
target_include_directories(EASTL PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/EASTL/include)
target_link_libraries(ocarina-ext PUBLIC EASTL)


add_subdirectory(tinyexr)
add_subdirectory(stblib)

cmake_policy(SET CMP0148 NEW)

# SPIRV-Cross is provided by the root Conan dependency graph.
if(VISION_BUILD_VULKAN)
    foreach(_ocarina_spirv_target IN ITEMS
        spirv-cross
        spirv-cross-c
        spirv-cross-core
        spirv-cross-glsl
        spirv-cross-hlsl
        spirv-cross-msl
        spirv-cross-cpp
        spirv-cross-reflect
        spirv-cross-util)
        if(TARGET ${_ocarina_spirv_target})
            set_property(TARGET ${_ocarina_spirv_target} PROPERTY FOLDER "ocarina/ext/spirv")
        endif()
    endforeach()
endif()
#list(APPEND SPIRV_LIBS "spirv-cross-core" "spirv-cross-hlsl" )

target_link_libraries(ocarina-ext
        PUBLIC
        ocarina-ext-stb
        ocarina-ext-tinyexr)
