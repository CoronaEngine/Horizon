set(hotfix_runtime_sources
    build_system.cpp
    file_tool.cpp
    hotfix.cpp
    module_interface.cpp
    staged_module.cpp)

add_library(horizon-hotfix-runtime-api INTERFACE)
target_include_directories(horizon-hotfix-runtime-api INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(horizon-hotfix-runtime-api INTERFACE horizon-hotfix-api horizon::fmt)

if(HORIZON_BUILD_VISION_HOTFIX)
    add_library(horizon-hotfix SHARED ${hotfix_runtime_sources}
        build_rules.cpp cmd_process.cpp compiler.cpp)
    target_compile_definitions(horizon-hotfix PUBLIC HORIZON_HOTFIX_LEGACY_BUILD=1)
    target_link_libraries(horizon-hotfix PUBLIC ocarina)
else()
    add_library(horizon-hotfix STATIC ${hotfix_runtime_sources})
endif()

find_package(Threads REQUIRED)
target_link_libraries(horizon-hotfix
    PUBLIC horizon-hotfix-runtime-api
    PRIVATE horizon-logging Threads::Threads ${CMAKE_DL_LIBS})

if(NOT HORIZON_BUILD_VISION_HOTFIX)
    return()
endif()

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
