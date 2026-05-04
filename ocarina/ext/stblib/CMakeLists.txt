
file(GLOB_RECURSE HEADER_FILES *.h*)
file(GLOB_RECURSE SOURCE_FILES *.c*)

add_library(ocarina-ext-stb SHARED ${HEADER_FILES} ${SOURCE_FILES})
target_include_directories(ocarina-ext-stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)

if (CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ocarina-ext-stb PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wno-tautological-compare>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)
endif ()

set_target_properties(ocarina-ext-stb PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")