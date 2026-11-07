file(GLOB_RECURSE HEADER_FILES *.h*)
file(GLOB_RECURSE SOURCE_FILES *.c*)

option(OCARINA_MATH_USE_KTM "Delegate ocarina-math operations to ktm" ON)

add_library(ocarina-math SHARED ${HEADER_FILES} ${SOURCE_FILES})
set_target_properties(ocarina-math PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
        FOLDER "ocarina")
target_compile_definitions(ocarina-math PRIVATE OC_MATH_EXPORT_DLL=1)
target_compile_definitions(ocarina-math
        PUBLIC OCARINA_MATH_USE_KTM=$<BOOL:${OCARINA_MATH_USE_KTM}>)
target_link_libraries(ocarina-math PUBLIC ocarina-core horizon::ktm)
