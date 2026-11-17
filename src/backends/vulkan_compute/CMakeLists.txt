file(GLOB_RECURSE HEADER_FILES "*.h")
file(GLOB_RECURSE SOURCE_FILES "*.cpp")

# Compile-only validation and executable pipelines share the production compiler.
# This library needs Vulkan headers, but never calls the Vulkan loader.
set(_slang_compiler_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/slang_compiler.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ast_to_slang_source.cpp")
list(REMOVE_ITEM SOURCE_FILES ${_slang_compiler_sources})
add_library(ocarina-slang-compiler STATIC ${_slang_compiler_sources})
set_target_properties(ocarina-slang-compiler PROPERTIES
    POSITION_INDEPENDENT_CODE ON FOLDER "ocarina")
target_link_libraries(ocarina-slang-compiler PRIVATE
    ocarina-dsl ocarina-generator ocarina-rhi
    slang::slang horizon::vulkan_headers
    vulkan-memory-allocator::vulkan-memory-allocator)

ocarina_add_backend(vulkan-compute SOURCES ${HEADER_FILES} ${SOURCE_FILES})

target_link_libraries(ocarina-backend-vulkan-compute PRIVATE
    Vulkan::Loader
    ocarina-slang-compiler
    slang::slang
    vulkan-memory-allocator::vulkan-memory-allocator
)

target_include_directories(ocarina-backend-vulkan-compute PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

helicon_install_runtime_deps(ocarina-backend-vulkan-compute)

# Copy Slang device builtin headers next to the backend DLL
set(BUILTIN_SLANG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/builtin/slang")
add_custom_target(copy_slang_headers ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Copying Slang builtin headers..."
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ocarina-backend-vulkan-compute>/slang"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${BUILTIN_SLANG_SRC}"
        "$<TARGET_FILE_DIR:ocarina-backend-vulkan-compute>/slang"
    COMMENT "Copying Slang device builtins"
)
add_dependencies(ocarina-backend-vulkan-compute copy_slang_headers)
