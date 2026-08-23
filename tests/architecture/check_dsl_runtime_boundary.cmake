if(NOT DEFINED DSL_SOURCE_DIR)
    message(FATAL_ERROR "DSL_SOURCE_DIR is required")
endif()
if(NOT DEFINED RUNTIME_SOURCE_DIR)
    message(FATAL_ERROR "RUNTIME_SOURCE_DIR is required")
endif()

foreach(legacy_directory IN ITEMS diagnostics tensor)
    set(legacy_path "${DSL_SOURCE_DIR}/${legacy_directory}")
    if(EXISTS "${legacy_path}")
        message(FATAL_ERROR
            "DSL runtime extraction is incomplete: ${legacy_path} still exists")
    endif()
endforeach()

set(runtime_headers
    data/encodable.h
    resources/registrable.h
    resources/polymorphic.h)

foreach(runtime_header IN LISTS runtime_headers)
    if(NOT EXISTS "${RUNTIME_SOURCE_DIR}/${runtime_header}")
        message(FATAL_ERROR
            "Runtime extraction is incomplete: ${RUNTIME_SOURCE_DIR}/${runtime_header} is missing")
    endif()
endforeach()

file(GLOB_RECURSE runtime_source_files
    "${RUNTIME_SOURCE_DIR}/*.h"
    "${RUNTIME_SOURCE_DIR}/*.hpp"
    "${RUNTIME_SOURCE_DIR}/*.cpp")

foreach(source_file IN LISTS runtime_source_files)
    file(STRINGS "${source_file}" dsl_namespace_declarations
        REGEX "namespace[ \t]+horizon::dsl[ \t]*[{]")
    if(dsl_namespace_declarations)
        message(FATAL_ERROR
            "Runtime source still declares the DSL namespace: ${source_file}")
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

    file(STRINGS "${source_file}" rhi_includes
        REGEX "#[ \t]*include[ \t]*[\"<]rhi/")
    if(rhi_includes)
        message(FATAL_ERROR
            "DSL source directly includes RHI after runtime extraction: ${source_file}")
    endif()
endforeach()
