horizon_add_test(horizon-test-legacy test_legacy.cpp test_public_headers.cpp)
add_dependencies(horizon-tests horizon-test-legacy)
helicon_install_runtime_deps(horizon-test-legacy)
set_tests_properties(horizon-test-legacy PROPERTIES LABELS "legacy;cpu" TIMEOUT 30)
add_test(NAME horizon.legacy.gpu COMMAND horizon-test-legacy --gpu)
set_tests_properties(horizon.legacy.gpu PROPERTIES
    LABELS "legacy;gpu" TIMEOUT 90 SKIP_RETURN_CODE 77)
