add_executable(horizon-test-dsl-ast test_dsl_ast.cpp)
target_link_libraries(horizon-test-dsl-ast PRIVATE horizon-dsl)
target_compile_features(horizon-test-dsl-ast PRIVATE cxx_std_20)

add_executable(horizon-test-dsl-statements test_dsl_statements.cpp)
target_link_libraries(horizon-test-dsl-statements PRIVATE horizon-dsl)
target_compile_features(horizon-test-dsl-statements PRIVATE cxx_std_20)

add_executable(horizon-test-dsl-function-corrector test_function_corrector.cpp)
target_link_libraries(horizon-test-dsl-function-corrector PRIVATE horizon-dsl)
target_compile_features(horizon-test-dsl-function-corrector PRIVATE cxx_std_20)

add_dependencies(horizon-tests
    horizon-test-dsl-ast
    horizon-test-dsl-function-corrector
    horizon-test-dsl-statements
)

add_test(NAME horizon.dsl.ast COMMAND horizon-test-dsl-ast)
add_test(NAME horizon.dsl.function_corrector COMMAND horizon-test-dsl-function-corrector)
add_test(NAME horizon.dsl.statements COMMAND horizon-test-dsl-statements)
