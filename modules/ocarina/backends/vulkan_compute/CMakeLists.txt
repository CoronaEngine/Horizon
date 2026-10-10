
find_package(Vulkan REQUIRED)
find_package(slang CONFIG REQUIRED)

file(GLOB_RECURSE HEADER_FILES "*.h")
file(GLOB_RECURSE SOURCE_FILES "*.cpp")

ocarina_add_backend(vulkan-compute SOURCES ${HEADER_FILES} ${SOURCE_FILES})

target_link_libraries(ocarina-backend-vulkan-compute PRIVATE
    Vulkan::Vulkan
    slang::slang
    vulkan-memory-allocator::vulkan-memory-allocator
)

target_include_directories(ocarina-backend-vulkan-compute PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
    foreach(_slang_bin_dir IN LISTS HORIZON_SLANG_BINARY_DIRS)
        file(GLOB _slang_runtime_dlls "${_slang_bin_dir}/*.dll")
        if(_slang_runtime_dlls)
            add_custom_command(TARGET ocarina-backend-vulkan-compute POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_slang_runtime_dlls} "$<TARGET_FILE_DIR:ocarina-backend-vulkan-compute>"
                COMMAND_EXPAND_LISTS VERBATIM)
        endif()
    endforeach()
endif()

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
