add_executable(horizon-test-legacy test_legacy.cpp test_public_headers.cpp)
target_link_libraries(horizon-test-legacy PRIVATE Horizon)
target_include_directories(horizon-test-legacy PRIVATE
    "${PROJECT_SOURCE_DIR}/tests"
    "${PROJECT_SOURCE_DIR}/src"
)
if(MSVC)
    target_compile_options(horizon-test-legacy PRIVATE /utf-8)
endif()

add_test(NAME horizon-test-legacy COMMAND horizon-test-legacy)
add_dependencies(horizon-tests horizon-test-legacy)
helicon_install_runtime_deps(horizon-test-legacy)
set_tests_properties(horizon-test-legacy PROPERTIES LABELS "legacy;cpu" TIMEOUT 30)
add_test(NAME horizon.legacy.gpu COMMAND horizon-test-legacy --gpu)
set_tests_properties(horizon.legacy.gpu PROPERTIES
    LABELS "legacy;gpu" TIMEOUT 90 SKIP_RETURN_CODE 77)
