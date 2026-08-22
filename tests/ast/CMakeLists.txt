add_executable(horizon-test-ast test_ast.cpp)
target_link_libraries(horizon-test-ast PRIVATE horizon-ast)
target_compile_features(horizon-test-ast PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-ast)

add_test(NAME horizon.ast COMMAND horizon-test-ast)
