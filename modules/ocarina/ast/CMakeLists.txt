file(GLOB_RECURSE HEADER_FILES *.h*)
file(GLOB_RECURSE SOURCE_FILES *.c*)

add_library(ocarina-ast SHARED ${SOURCE_FILES})
set_target_properties(ocarina-ast PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
		FOLDER "ocarina")
target_compile_definitions(ocarina-ast PRIVATE OC_AST_EXPORT_DLL=1)
target_link_libraries(ocarina-ast
        PUBLIC
        ocarina-ext
        ocarina-core)