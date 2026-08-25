add_executable(horizon-test-dsl test_dsl.cpp)
target_link_libraries(horizon-test-dsl PRIVATE horizon-dsl)
target_compile_features(horizon-test-dsl PRIVATE cxx_std_20)

add_executable(horizon-test-dsl-texture test_texture.cpp)
target_link_libraries(horizon-test-dsl-texture PRIVATE horizon-dsl)
target_compile_features(horizon-test-dsl-texture PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-dsl horizon-test-dsl-texture)

add_test(NAME horizon.dsl COMMAND horizon-test-dsl)
add_test(NAME horizon.dsl.texture COMMAND horizon-test-dsl-texture)
