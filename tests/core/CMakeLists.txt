add_executable(horizon-test-core-tuple test_tuple.cpp)
target_include_directories(horizon-test-core-tuple PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(horizon-test-core-tuple PRIVATE cxx_std_20)
add_dependencies(horizon-tests horizon-test-core-tuple)
add_test(NAME horizon.core.tuple COMMAND horizon-test-core-tuple)

add_executable(horizon-test-core-trace-mode-selection test_trace_mode_selection.cpp)
target_include_directories(horizon-test-core-trace-mode-selection PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(horizon-test-core-trace-mode-selection PRIVATE cxx_std_20)
add_dependencies(horizon-tests horizon-test-core-trace-mode-selection)
add_test(NAME horizon.core.trace_mode_selection COMMAND horizon-test-core-trace-mode-selection)
set_tests_properties(horizon.core.trace_mode_selection PROPERTIES LABELS "horizon;cpu")

# Exercise the real allocator across a DLL boundary without engine dependencies.
add_library(horizon-allocator-crt-fixture SHARED
    allocator_crt_fixture.cpp "${PROJECT_SOURCE_DIR}/src/core/stl.cpp")
target_include_directories(horizon-allocator-crt-fixture PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(horizon-allocator-crt-fixture PUBLIC cxx_std_20)
target_compile_definitions(horizon-allocator-crt-fixture PRIVATE OC_CORE_EXPORT_DLL=1)

add_executable(horizon-test-core-allocator-crt test_allocator_crt.cpp)
target_link_libraries(horizon-test-core-allocator-crt PRIVATE horizon-allocator-crt-fixture)
set_target_properties(horizon-allocator-crt-fixture horizon-test-core-allocator-crt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/allocator-crt"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/allocator-crt")
add_dependencies(horizon-tests horizon-test-core-allocator-crt)
add_test(NAME horizon.core.allocator_crt COMMAND horizon-test-core-allocator-crt)
set_tests_properties(horizon.core.allocator_crt PROPERTIES LABELS "horizon;cpu" TIMEOUT 30)

add_executable(horizon-test-logging-configuration test_logging_configuration.cpp)
target_link_libraries(horizon-test-logging-configuration PRIVATE horizon-logging)
add_dependencies(horizon-tests horizon-test-logging-configuration)
add_test(NAME horizon.core.logging_configuration COMMAND horizon-test-logging-configuration)
add_test(NAME horizon.core.logging_relative_file COMMAND horizon-test-logging-configuration relative-file)

add_executable(horizon-test-core-public-headers test_public_headers.cpp)
target_link_libraries(horizon-test-core-public-headers PRIVATE horizon-logging)
target_compile_definitions(horizon-test-core-public-headers PRIVATE CFW_ENABLE_LOCK_TIMEOUT=1)
if(WIN32)
    target_link_libraries(horizon-test-core-public-headers PRIVATE dbghelp)
endif()
if(MSVC)
    target_compile_options(horizon-test-core-public-headers PRIVATE /utf-8)
endif()
add_dependencies(horizon-tests horizon-test-core-public-headers)
add_test(NAME horizon.core.public_headers COMMAND horizon-test-core-public-headers)

if(HORIZON_BUILD_ENGINE AND NOT HORIZON_USE_LEGACY_BACKEND)
    # Check the engine's public headers and transitive logging dependency.
    target_link_libraries(horizon-test-logging-configuration PRIVATE Horizon)
    target_compile_definitions(horizon-test-logging-configuration PRIVATE HORIZON_TEST_ENGINE_LOGGING)
endif()
