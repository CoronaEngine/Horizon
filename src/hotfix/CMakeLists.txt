file(GLOB HEADER_FILES *.h*)
file(GLOB SOURCE_FILES *.c*)

add_library(horizon-hotfix SHARED ${SOURCE_FILES})
target_include_directories(horizon-hotfix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/ocarina)
target_link_libraries(horizon-hotfix PUBLIC
        ocarina)

if (MSVC)
    set(CPP_COMPILE_TYPE "msvc")
else ()
    set(CPP_COMPILE_TYPE "llvm")
endif ()

target_compile_definitions(horizon-hotfix PRIVATE
        VS_EXPORT_DLL=1
        CPP_COMPILER_PATH="${CMAKE_CXX_COMPILER}"
        CPP_COMPILER_TYPE="${CPP_COMPILE_TYPE}"
        -DCMAKE_VERBOSE_MAKEFILE=ON)
set_target_properties(horizon-hotfix PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_subdirectory(rules_parsers)
add_subdirectory(compiles)
add_subdirectory(test)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/horizon_hotfix_all.cpp"
        "namespace horizon::hotfix_bundle { void all_anchor() {} }\n")
add_library(horizon-hotfix-all STATIC
        "${CMAKE_CURRENT_BINARY_DIR}/horizon_hotfix_all.cpp")

target_link_libraries(horizon-hotfix-all PUBLIC
        horizon-hotfix
        horizon-hotfix-compiler
        horizon-hotfix-test
        horizon-hotfix-rules_parser)

set_property(TARGET horizon-hotfix-all PROPERTY
        INTERFACE_HORIZON_RUNTIME_DEPS
        "$<TARGET_FILE:horizon-hotfix-compiler-msvc>"
        "$<TARGET_FILE:horizon-hotfix-compiler-llvm>"
        "$<TARGET_FILE:horizon-hotfix-rules_parser-ninja>"
        "$<TARGET_FILE:horizon-hotfix-rules_parser-json>")
