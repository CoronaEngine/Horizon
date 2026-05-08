function(ocarina_add_backend name)
    cmake_parse_arguments(BACKEND "" "" "SOURCES" ${ARGN})
    add_library(ocarina-backend-${name} MODULE ${BACKEND_SOURCES})
    target_link_libraries(ocarina-backend-${name} PRIVATE
            ocarina-ast
            ocarina-dsl
            ocarina-generator)
    add_dependencies(ocarina-backend ocarina-backend-${name} copy_cuda_headers)
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

option(VISION_BUILD_VULKAN "VISION BUILD VULKAN" OFF)

if (VISION_BUILD_VULKAN)
    add_subdirectory(vulkan)
endif ()

