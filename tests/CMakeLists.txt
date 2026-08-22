add_custom_target(horizon-tests)

add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(ast)

add_dependencies(horizon-tests
    horizon-core
    horizon-math
    horizon-ast
    horizon-dsl
)

add_test(
    NAME horizon.architecture.core_math_boundary
    COMMAND ${CMAKE_COMMAND}
            -DCORE_SOURCE_DIR=${PROJECT_SOURCE_DIR}/src/core
            -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/check_core_math_boundary.cmake
)
