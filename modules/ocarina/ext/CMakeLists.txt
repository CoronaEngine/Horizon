
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

target_link_libraries(ocarina-ext
        PUBLIC
        ocarina-ext-stb
        ocarina-ext-tinyexr)