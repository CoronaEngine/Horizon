include_guard(GLOBAL)

if(NOT DEFINED ENV{CUDA_PATH})
    return()
endif()

include_directories(${PROJECT_SOURCE_DIR}/modules/ocarina)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")

set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)

add_subdirectory(modules/ocarina)

# 由根级负责把共享依赖装配到 ocarina-ext 上，子目录不再引用这些库的目标名
if(TARGET ocarina-ext)
    target_link_libraries(ocarina-ext PUBLIC
        fmt::fmt-header-only
        spdlog
        xxhash)
endif()