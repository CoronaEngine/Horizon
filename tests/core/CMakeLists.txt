add_executable(horizon-test-core-tuple test_tuple.cpp)
target_link_libraries(horizon-test-core-tuple PRIVATE horizon-core)
target_compile_features(horizon-test-core-tuple PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-core-tuple)

add_test(NAME horizon.core.tuple COMMAND horizon-test-core-tuple)
