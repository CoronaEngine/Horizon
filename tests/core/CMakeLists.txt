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
