if(NOT DEFINED DSL_SOURCE_DIR)
    message(FATAL_ERROR "DSL_SOURCE_DIR is required")
endif()

foreach(legacy_directory IN ITEMS diagnostics tensor)
    set(legacy_path "${DSL_SOURCE_DIR}/${legacy_directory}")
    if(EXISTS "${legacy_path}")
        message(FATAL_ERROR
            "DSL runtime extraction is incomplete: ${legacy_path} still exists")
    endif()
endforeach()

file(GLOB_RECURSE dsl_source_files
    "${DSL_SOURCE_DIR}/*.h"
    "${DSL_SOURCE_DIR}/*.hpp"
    "${DSL_SOURCE_DIR}/*.cpp")

foreach(source_file IN LISTS dsl_source_files)
    file(STRINGS "${source_file}" legacy_runtime_references
        REGEX "(diagnostics|tensor)/")
    if(legacy_runtime_references)
        message(FATAL_ERROR
            "DSL source still references a removed runtime path: ${source_file}")
    endif()
endforeach()
