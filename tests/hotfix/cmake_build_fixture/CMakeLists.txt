cmake_minimum_required(VERSION 4.0)
project(HotfixBuildFixture LANGUAGES CXX)

add_library(fixture_module MODULE fixture.cpp)
set_target_properties(fixture_module PROPERTIES
    PREFIX ""
    OUTPUT_NAME "actual module"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    CXX_VISIBILITY_PRESET default
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/published modules/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/published modules/$<CONFIG>")

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/expected-module-names/$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE_NAME:fixture_module>\n"
    NEWLINE_STYLE LF)

# The backend must select the module rather than build the default target.
add_custom_target(unrelated_target ALL COMMAND "${CMAKE_COMMAND}" -E false)

include("${HORIZON_HOTFIX_MANIFEST_MODULE}")
horizon_generate_hotfix_manifest(fixture_module)
