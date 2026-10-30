include_guard(GLOBAL)

# Consume the Conan config explicitly; never fall back to the system SDK module.
find_package(VulkanLoader CONFIG REQUIRED)
if(NOT TARGET Vulkan::Loader)
    message(FATAL_ERROR "The Conan VulkanLoader package must provide Vulkan::Loader")
endif()

if(WIN32)
    set(_horizon_vulkan_runtime)
    foreach(_config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${_config}" _upper_config)
        foreach(_bin_dir IN LISTS vulkan-loader_BIN_DIRS_${_upper_config})
            set(_dll "${_bin_dir}/vulkan-1.dll")
            if(NOT EXISTS "${_dll}")
                message(FATAL_ERROR "Conan Vulkan loader runtime is missing: ${_dll}")
            endif()
            list(APPEND _horizon_vulkan_runtime "$<$<CONFIG:${_config}>:${_dll}>")
        endforeach()
    endforeach()
    if(NOT _horizon_vulkan_runtime)
        message(FATAL_ERROR "The Conan VulkanLoader package did not expose a runtime bin directory")
    endif()
    set_property(GLOBAL PROPERTY HORIZON_VULKAN_RUNTIME_FILES "${_horizon_vulkan_runtime}")
endif()

function(horizon_install_vulkan_runtime target_name)
    if(NOT WIN32)
        return()
    endif()
    get_property(_runtime GLOBAL PROPERTY HORIZON_VULKAN_RUNTIME_FILES)
    # Recopy if the package changes, even when the consumer only uses Volk.
    set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS "${_runtime}")
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_runtime} "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying Conan Vulkan loader to ${target_name}"
        VERBATIM)
endfunction()
