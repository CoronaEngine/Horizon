include_guard(GLOBAL)

if(NOT HORIZON_BUILD_OCARINA)
    return()
endif()

include_directories(${PROJECT_SOURCE_DIR}/src)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")

set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)

add_library(ocarina-include INTERFACE)
target_include_directories(ocarina-include INTERFACE ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(src/ext)
add_subdirectory(src/core)
add_subdirectory(src/math)
add_subdirectory(src/ast)
add_subdirectory(src/dsl)
add_subdirectory(src/generator)
add_subdirectory(src/rhi)
add_subdirectory(src/backends)

add_library(ocarina-native INTERFACE)
target_link_libraries(ocarina-native INTERFACE
        ocarina-ext
        ocarina-math
        ocarina-dsl
        ocarina-ast
        ocarina-generator
        ocarina-backend
        ocarina-rhi
        ocarina-core)

add_library(ocarina SHARED src/main.cpp)
target_link_libraries(ocarina ocarina-native)


# ocarina tests are opt-in from the root Horizon project.
if(HORIZON_BUILD_OCARINA_TESTS)
    add_subdirectory(src/tests)
endif()

set_target_properties(ocarina PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

# 由根级负责把共享依赖装配到 ocarina-ext 上，子目录不再引用这些库的目标名
if(TARGET ocarina-ext)
    target_link_libraries(ocarina-ext PUBLIC
        horizon::fmt
        horizon::spdlog
        horizon::xxhash)
endif()
