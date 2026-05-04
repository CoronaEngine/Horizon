
add_library(ocarina-ext SHARED empty.cpp)
set_target_properties(ocarina-ext PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

add_library(xxhash INTERFACE)
target_include_directories(xxhash INTERFACE xxHash)
target_compile_definitions(xxhash INTERFACE XXH_INLINE_ALL)
set_target_properties(xxhash PROPERTIES
        UNITY_BUILD OFF)

set(FMT_OS OFF CACHE BOOL "" FORCE)
add_subdirectory(fmt)
target_compile_definitions(fmt-header-only INTERFACE
        FMT_EXCEPTIONS=0
        FMT_HEADER_ONLY=1
        FMT_USE_NOEXCEPT=1)
target_include_directories(fmt-header-only INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fmt/include)

target_link_libraries(ocarina-ext PUBLIC fmt::fmt-header-only)

add_subdirectory(EASTL)
target_include_directories(EASTL PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/EASTL/include)
target_link_libraries(ocarina-ext PUBLIC EASTL)


add_subdirectory(tinyexr)

if(NOT TARGET spdlog)
    add_subdirectory(spdlog)
endif()
set_target_properties(spdlog PROPERTIES
        UNITY_BUILD FALSE)
target_link_libraries(spdlog PUBLIC fmt::fmt-header-only)
target_link_libraries(ocarina-ext PUBLIC spdlog)
add_subdirectory(stblib)

add_subdirectory(SDL)
set_property(TARGET SDL_uclibc PROPERTY FOLDER "ocarina/ext/SDL3")
set_property(TARGET SDL3_test PROPERTY FOLDER "ocarina/ext/SDL3")
set_property(TARGET SDL3-shared PROPERTY FOLDER "ocarina/ext/SDL3")

cmake_policy(SET CMP0148 NEW)

if (${OCARINA_PY})
    add_subdirectory(pybind11)
    find_package(Python REQUIRED COMPONENTS Interpreter Development)
    include_directories(${PYTHON_INCLUDE_DIRS})
    link_directories(${PYTHON_LIBRARIES})
    target_include_directories(ocarina-ext INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/pybind11/include/pybind11
            ${CMAKE_CURRENT_SOURCE_DIR}/pybind11/include
            ${PYTHON_INCLUDE_DIRS})
endif ()

if(VISION_BUILD_VULKAN)
    if(NOT TARGET spirv-cross-core)
        add_subdirectory(SPIRV-Cross)
    endif()
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
        ${PYTHON_LIBRARIES}
        xxhash
        spdlog
        ocarina-ext-tinyexr
        SDL3::SDL3)