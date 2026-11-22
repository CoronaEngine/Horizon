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

add_subdirectory(src/core)
add_subdirectory(src/math)
add_subdirectory(src/ast)
add_subdirectory(src/dsl)
add_subdirectory(src/generator)
add_subdirectory(src/rhi)
add_subdirectory(src/backends)

add_library(ocarina-native INTERFACE)
target_link_libraries(ocarina-native INTERFACE
        ocarina-math
        ocarina-dsl
        ocarina-ast
        ocarina-generator
        ocarina-backend
        ocarina-rhi
        ocarina-core)

add_library(ocarina SHARED src/main.cpp)
target_link_libraries(ocarina ocarina-native)

# The Ocarina DSL example lives in the root examples/ tree and is dispatched by
# the existing HorizonExamples host. examples/ is configured before this file,
# so attach the Ocarina-dependent sources and libraries here, after the Ocarina
# targets exist.
if(HORIZON_BUILD_EXAMPLES AND TARGET HorizonExamples)
    target_sources(HorizonExamples PRIVATE
        ${PROJECT_SOURCE_DIR}/examples/example_ocarina_dsl/example_ocarina_dsl.cpp
        # Compile the backend's Slang emitter into the example so it can show
        # the generated source before device.compile() compiles the same kernel.
        ${PROJECT_SOURCE_DIR}/src/backends/vulkan_compute/ast_to_slang_source.cpp)
    target_include_directories(HorizonExamples PRIVATE
        ${PROJECT_SOURCE_DIR}/src/backends/vulkan_compute)
    target_link_libraries(HorizonExamples PRIVATE
        ocarina-native
        horizon::vulkan_headers
        vulkan-memory-allocator::vulkan-memory-allocator)
    target_compile_definitions(HorizonExamples PRIVATE HORIZON_OCARINA_EXAMPLE_ENABLED=1)
    # Keep the example next to the Ocarina backend DLL and the copied slang/
    # headers so RHIContext can load the backend from the executable directory.
    set_target_properties(HorizonExamples PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    add_dependencies(HorizonExamples ocarina-backend-vulkan-compute copy_slang_headers)
endif()


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
