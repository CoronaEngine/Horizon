add_executable(VulkanTutorialBaseline 28_model_loading.cpp)
# The chapter uses the five-argument LoadObj API from tinyobjloader v1.0.6.
# https://github.com/tinyobjloader/tinyobjloader/blob/v1.0.6/tiny_obj_loader.h
target_include_directories(VulkanTutorialBaseline SYSTEM PRIVATE third_party/tinyobjloader-1.0.6)
target_link_libraries(VulkanTutorialBaseline PRIVATE Vulkan::Loader horizon::vulkan_headers
    horizon::glfw horizon::glm horizon::stb)
horizon_install_vulkan_runtime(VulkanTutorialBaseline)

# The tutorial opens all four resources relative to its working directory.
set(runtime "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
set_target_properties(VulkanTutorialBaseline PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${runtime}"
    VS_DEBUGGER_WORKING_DIRECTORY "${runtime}")
foreach(resource IN ITEMS models/viking_room.obj textures/viking_room.png)
    get_filename_component(directory "${resource}" DIRECTORY)
    add_custom_command(OUTPUT "${runtime}/${resource}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${runtime}/${directory}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../assets/${resource}" "${runtime}/${resource}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/${resource}" VERBATIM)
    list(APPEND resources "${runtime}/${resource}")
endforeach()

find_program(TUTORIAL_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
foreach(stage IN ITEMS vert frag)
    set(shader "${CMAKE_CURRENT_SOURCE_DIR}/27_shader_depth.${stage}")
    set(output "${runtime}/shaders/${stage}.spv")
    if(TUTORIAL_GLSLC)
        set(compile_command "${TUTORIAL_GLSLC}" "${shader}" -o "${output}")
        set(input "${shader}")
    else()
        set(input "${CMAKE_CURRENT_SOURCE_DIR}/${stage}.spv")
        set(compile_command "${CMAKE_COMMAND}" -E copy_if_different "${input}" "${output}")
    endif()
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${runtime}/shaders"
        COMMAND ${compile_command}
        DEPENDS "${input}" VERBATIM)
    list(APPEND resources "${output}")
endforeach()
add_custom_target(VulkanTutorialResources DEPENDS ${resources})
add_dependencies(VulkanTutorialBaseline VulkanTutorialResources)
