file(GLOB_RECURSE HEADER_FILES *.h*)
file(GLOB_RECURSE SOURCE_FILES *.c*)

add_library(ocarina-rhi SHARED ${HEADER_FILES} ${SOURCE_FILES})
set_target_properties(ocarina-rhi PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
        FOLDER "ocarina")
target_compile_definitions(ocarina-rhi PRIVATE OC_RHI_EXPORT_DLL=1)
target_link_libraries(ocarina-rhi PUBLIC ocarina-generator)
