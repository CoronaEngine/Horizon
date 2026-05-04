

add_library(ocarina-include INTERFACE)
target_include_directories(ocarina-include INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(ext)
add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(ast)
add_subdirectory(dsl)
add_subdirectory(generator)
add_subdirectory(rhi)
add_subdirectory(backends)

add_library(ocarina-native INTERFACE)
target_link_libraries(ocarina-native INTERFACE
        ocarina-ext
        ocarina-math
        ocarina-dsl
        ocarina-ast
        ocarina-generator
        ocarina-backend
        ocarina-rhi
        ocarina-core)

add_library(ocarina SHARED main.cpp)
target_link_libraries(ocarina ocarina-native)


option(OCARINA_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})

if (${OCARINA_BUILD_TESTS})
    add_subdirectory(tests)
endif ()

set_target_properties(ocarina PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

