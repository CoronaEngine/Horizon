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

target_link_libraries(ocarina-ext
        PUBLIC
        ocarina-ext-stb
        ocarina-ext-tinyexr)
