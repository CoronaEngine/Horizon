add_custom_target(horizon-tests)

add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(ast)
add_subdirectory(image)
add_subdirectory(dsl)

add_dependencies(horizon-tests
    horizon-core
    horizon-math
    horizon-ast
    horizon-dsl
    horizon-image
)

add_test(
    NAME horizon.architecture.core_math_boundary
    COMMAND ${CMAKE_COMMAND}
            -DCORE_SOURCE_DIR=${PROJECT_SOURCE_DIR}/src/core
            -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/check_core_math_boundary.cmake
)

add_test(
    NAME horizon.architecture.image_module_boundary
    COMMAND ${CMAKE_COMMAND}
            -DHORIZON_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/check_image_module_boundary.cmake
)

add_test(
    NAME horizon.architecture.dsl_runtime_boundary
    COMMAND ${CMAKE_COMMAND}
            -DDSL_SOURCE_DIR=${PROJECT_SOURCE_DIR}/src/dsl
            -DRUNTIME_SOURCE_DIR=${PROJECT_SOURCE_DIR}/src/runtime
            -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/check_dsl_runtime_boundary.cmake
)
