add_library(ocarina-ext SHARED empty.cpp)
set_target_properties(ocarina-ext PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

# fmt and xxHash are provided by the root Conan dependency graph and
# linked to ocarina-ext from the Horizon root.

# Keep one mimalloc instance in ocarina-core, behind its exported allocator API.
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
set(MI_XMALLOC ON CACHE BOOL "" FORCE)
set(MI_USE_CXX OFF CACHE BOOL "" FORCE)
set(MI_OSX_ZONE ON CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(mimalloc)
if(MSVC)
    target_compile_options(mimalloc-static PRIVATE /W0)
    target_compile_definitions(mimalloc-static PUBLIC _CRT_SECURE_NO_WARNINGS=1)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(mimalloc-static PRIVATE -Wno-strict-prototypes)
endif()
set_target_properties(mimalloc-static PROPERTIES UNITY_BUILD ON)

add_subdirectory(tinyexr)
add_subdirectory(stblib)

cmake_policy(SET CMP0148 NEW)

target_link_libraries(ocarina-ext
        PUBLIC
        ocarina-ext-stb
        ocarina-ext-tinyexr)
