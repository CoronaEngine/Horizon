function(ocarina_add_backend name)
    cmake_parse_arguments(_ocarina_backend "" "" "SOURCES" ${ARGN})
    add_library(ocarina-backend-${name} MODULE ${_ocarina_backend_SOURCES})
    target_link_libraries(ocarina-backend-${name} PRIVATE
            ocarina-ast
            ocarina-dsl
            ocarina-generator)
    # copy_cuda_headers only exists when CUDAToolkit is found; don't force it on
    # every backend (e.g. vulkan-compute has no CUDA dependency).
    add_dependencies(ocarina-backend ocarina-backend-${name})
    set_target_properties(ocarina-backend-${name} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
            WINDOWS_EXPORT_ALL_SYMBOLS OFF
            UNITY_BUILD OFF
            DEBUG_POSTFIX ""
            FOLDER "ocarina")
endfunction()

add_library(ocarina-backend INTERFACE)

add_subdirectory(cuda)

# Vulkan compute backend — opt-in when Vulkan is available.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_subdirectory(vulkan_compute)
else()
    message(STATUS "Vulkan not found, skipping vulkan_compute backend build.")
endif()
