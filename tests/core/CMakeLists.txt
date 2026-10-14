add_executable(horizon-test-core-tuple test_tuple.cpp)
target_link_libraries(horizon-test-core-tuple PRIVATE horizon-core)
target_compile_features(horizon-test-core-tuple PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-core-tuple)

add_test(NAME horizon.core.tuple COMMAND horizon-test-core-tuple)

add_library(horizon-test-dynamic-module MODULE test_dynamic_module_library.cpp)
target_include_directories(horizon-test-dynamic-module PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_compile_features(horizon-test-dynamic-module PRIVATE cxx_std_20)

add_executable(horizon-test-core-platform test_dynamic_module.cpp)
target_link_libraries(horizon-test-core-platform PRIVATE horizon-core)
target_compile_features(horizon-test-core-platform PRIVATE cxx_std_20)

add_dependencies(horizon-tests
    horizon-test-dynamic-module
    horizon-test-core-platform
)

add_test(
    NAME horizon.core.platform
    COMMAND horizon-test-core-platform $<TARGET_FILE:horizon-test-dynamic-module>
)

add_executable(horizon-test-core-logging test_logging.cpp)
target_link_libraries(horizon-test-core-logging PRIVATE horizon-core)
target_compile_features(horizon-test-core-logging PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-core-logging)

add_test(NAME horizon.core.logging COMMAND horizon-test-core-logging)

add_executable(horizon-test-logging-compat test_logging.cpp)
target_link_libraries(horizon-test-logging-compat PRIVATE horizon-core quill::quill)
target_include_directories(horizon-test-logging-compat PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(horizon-test-logging-compat PRIVATE HORIZON_TEST_LEGACY_LOGGING)
add_dependencies(horizon-tests horizon-test-logging-compat)
add_test(NAME horizon.core.logging_compat_core_first COMMAND horizon-test-logging-compat)
add_test(NAME horizon.core.logging_compat_legacy_first COMMAND horizon-test-logging-compat legacy-first)

add_executable(horizon-test-logging-configuration test_logging_configuration.cpp)
target_link_libraries(horizon-test-logging-configuration PRIVATE horizon-core quill::quill)
target_include_directories(horizon-test-logging-configuration PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_dependencies(horizon-tests horizon-test-logging-configuration)
add_test(NAME horizon.core.logging_configuration COMMAND horizon-test-logging-configuration)
add_test(NAME horizon.core.logging_relative_file COMMAND horizon-test-logging-configuration relative-file)

add_executable(horizon-test-core-public-headers test_public_headers.cpp)
target_link_libraries(horizon-test-core-public-headers PRIVATE horizon-core)
target_compile_definitions(horizon-test-core-public-headers PRIVATE CFW_ENABLE_LOCK_TIMEOUT=1)
if(MSVC)
    target_compile_options(horizon-test-core-public-headers PRIVATE /utf-8)
endif()
add_dependencies(horizon-tests horizon-test-core-public-headers)
add_test(NAME horizon.core.public_headers COMMAND horizon-test-core-public-headers)

if(HORIZON_BUILD_ENGINE AND NOT HORIZON_USE_LEGACY_BACKEND)
    # Check both header orders and the engine's transitive Core dependency.
    foreach(target horizon-test-logging-compat horizon-test-logging-configuration)
        target_link_libraries(${target} PRIVATE Horizon)
        target_compile_definitions(${target} PRIVATE HORIZON_TEST_ENGINE_LOGGING)
    endforeach()
endif()
