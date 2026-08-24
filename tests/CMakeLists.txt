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

set(_quill_logging_boundary_build_dir
    "${CMAKE_CURRENT_BINARY_DIR}/quill-logging-boundary")
add_test(
    NAME horizon.architecture.quill_logging_boundary
    COMMAND ${CMAKE_COMMAND} -E env CORONA_DEV_BOOTSTRAP_ACTIVE=1
            ${CMAKE_COMMAND}
            -S ${PROJECT_SOURCE_DIR}
            -B ${_quill_logging_boundary_build_dir}
            -G "${CMAKE_GENERATOR}"
            -DHORIZON_DEV_CONFIGURATION=${HORIZON_DEV_CONFIGURATION}
            -DHORIZON_DEV_TARGET_FAMILY=core
            -DHORIZON_BUILD_ENGINE=OFF
            -DHORIZON_BUILD_TESTS=OFF
            -DCMAKE_DISABLE_FIND_PACKAGE_spdlog=TRUE
)
