horizon_add_test(HorizonTests
    main.cpp
    vulkan/test_execution_system.cpp
    vulkan/test_hardware_context.cpp
)

set_tests_properties(HorizonTests
    PROPERTIES
        SKIP_RETURN_CODE 77
)
