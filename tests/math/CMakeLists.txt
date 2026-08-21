add_executable(horizon-test-math-swizzle test_swizzle.cpp)
target_link_libraries(horizon-test-math-swizzle PRIVATE horizon-math)
target_compile_features(horizon-test-math-swizzle PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-math-swizzle)

add_test(NAME horizon.math.swizzle COMMAND horizon-test-math-swizzle)
