file(GLOB HEADER_FILES *.h*)
file(GLOB SOURCE_FILES *.c*)

add_library(horizon-hotfix-test SHARED ${SOURCE_FILES})
target_link_libraries(horizon-hotfix-test PUBLIC
        ocarina
        horizon-hotfix
        ocarina-include)

set_target_properties(horizon-hotfix-test PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
