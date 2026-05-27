set(HORIZON_TEST_COMMON_SOURCES
    common/test_main.cpp
)

horizon_add_test(HorizonSmokeTests
    ${HORIZON_TEST_COMMON_SOURCES}
    smoke/test_smoke.cpp
)
