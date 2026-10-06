
find_package(Vulkan REQUIRED)
find_package(slang CONFIG REQUIRED)

file(GLOB_RECURSE HEADER_FILES "*.h")
file(GLOB_RECURSE SOURCE_FILES "*.cpp")

ocarina_add_backend(vulkan-compute SOURCES ${HEADER_FILES} ${SOURCE_FILES})

target_link_libraries(ocarina-backend-vulkan-compute PRIVATE
    Vulkan::Vulkan
    slang::slang
)

target_include_directories(ocarina-backend-vulkan-compute PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Copy Slang device builtin headers next to the backend DLL
set(BUILTIN_SLANG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/builtin/slang")
add_custom_target(copy_slang_headers ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Copying Slang builtin headers..."
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ocarina-backend-vulkan-compute>/slang"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${BUILTIN_SLANG_SRC}"
        "$<TARGET_FILE_DIR:ocarina-backend-vulkan-compute>/slang"
    DEPENDS ocarina-backend-vulkan-compute
    COMMENT "Copying Slang device builtins"
)
