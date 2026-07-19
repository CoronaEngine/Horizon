file(GLOB HEADER_FILES *.h*)
file(GLOB SOURCE_FILES *.c*)

add_library(vision-hotfix SHARED ${SOURCE_FILES})
target_include_directories(vision-hotfix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/ocarina)
target_link_libraries(vision-hotfix PUBLIC
        ocarina)

if (MSVC)
    set(CPP_COMPILE_TYPE "msvc")
else ()
    set(CPP_COMPILE_TYPE "llvm")
endif ()

target_compile_definitions(vision-hotfix PRIVATE
        VS_EXPORT_DLL=1
        CPP_COMPILER_PATH="${CMAKE_CXX_COMPILER}"
        CPP_COMPILER_TYPE="${CPP_COMPILE_TYPE}"
        -DCMAKE_VERBOSE_MAKEFILE=ON)
set_target_properties(vision-hotfix PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_subdirectory(rules_parsers)
add_subdirectory(compiles)
add_subdirectory(test)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vision_hotfix_all.cpp"
        "namespace horizon::vision_hotfix { void all_anchor() {} }\n")
add_library(vision-hotfix-all STATIC
        "${CMAKE_CURRENT_BINARY_DIR}/vision_hotfix_all.cpp")

target_link_libraries(vision-hotfix-all PUBLIC
        vision-hotfix
        vision-hotfix-compiler
        vision-hotfix-test
        vision-hotfix-rules_parser)

set_property(TARGET vision-hotfix-all PROPERTY
        INTERFACE_HORIZON_RUNTIME_DEPS
        "$<TARGET_FILE:vision-hotfix-compiler-msvc>"
        "$<TARGET_FILE:vision-hotfix-compiler-llvm>"
        "$<TARGET_FILE:vision-hotfix-rules_parser-ninja>"
        "$<TARGET_FILE:vision-hotfix-rules_parser-json>")
