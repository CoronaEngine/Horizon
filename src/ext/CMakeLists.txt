add_library(ocarina-ext SHARED empty.cpp)
set_target_properties(ocarina-ext PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

# fmt and xxHash are provided by the root Conan dependency graph and
# linked to ocarina-ext from the Horizon root.
