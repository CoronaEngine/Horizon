file(GLOB_RECURSE HEADER_FILES "*.h")
file(GLOB_RECURSE SOURCE_FILES "*.cpp")

ocarina_add_backend(vulkan-compute SOURCES ${HEADER_FILES} ${SOURCE_FILES})

target_link_libraries(ocarina-backend-vulkan-compute PRIVATE
    Vulkan::Loader
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
