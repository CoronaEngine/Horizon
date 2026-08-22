add_executable(horizon-test-image test_image.cpp)
target_link_libraries(horizon-test-image PRIVATE horizon-image horizon::tinyexr)
target_compile_features(horizon-test-image PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-image)

add_test(NAME horizon.image COMMAND horizon-test-image)
