add_executable(horizon-test-dsl test_dsl.cpp)
target_link_libraries(horizon-test-dsl PRIVATE horizon-dsl)
target_compile_features(horizon-test-dsl PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-dsl)

add_test(NAME horizon.dsl COMMAND horizon-test-dsl)
