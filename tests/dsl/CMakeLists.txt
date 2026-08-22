add_executable(horizon-test-dsl-ast test_dsl_ast.cpp)
target_link_libraries(horizon-test-dsl-ast PRIVATE horizon-dsl)
target_compile_features(horizon-test-dsl-ast PRIVATE cxx_std_20)

add_executable(horizon-test-dsl-statements test_dsl_statements.cpp)
target_link_libraries(horizon-test-dsl-statements PRIVATE horizon-dsl)
target_compile_features(horizon-test-dsl-statements PRIVATE cxx_std_20)

add_dependencies(horizon-tests
    horizon-test-dsl-ast
    horizon-test-dsl-statements
)

add_test(NAME horizon.dsl.ast COMMAND horizon-test-dsl-ast)
add_test(NAME horizon.dsl.statements COMMAND horizon-test-dsl-statements)
