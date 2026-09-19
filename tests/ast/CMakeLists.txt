add_executable(horizon-test-ast test_ast.cpp)
target_link_libraries(horizon-test-ast PRIVATE horizon-ast)
target_compile_features(horizon-test-ast PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-ast)

add_test(NAME horizon.ast COMMAND horizon-test-ast)

add_executable(horizon-test-ast-raster test_raster.cpp)
target_link_libraries(horizon-test-ast-raster PRIVATE horizon-ast)
target_compile_features(horizon-test-ast-raster PRIVATE cxx_std_20)
add_dependencies(horizon-tests horizon-test-ast-raster)
add_test(NAME horizon.ast.raster COMMAND horizon-test-ast-raster)
add_test(NAME horizon.ast.raster.kernel_rejection
    COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:horizon-test-ast-raster>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_raster_rejection.cmake)
