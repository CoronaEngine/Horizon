horizon_add_test(HorizonTests
    main.cpp
    public_header_syntax.cpp
    vulkan/test_diagnostics.cpp
    vulkan/test_hardware_buffer.cpp
    vulkan/test_hardware_image.cpp
    vulkan/test_execution_system.cpp
    vulkan/test_hardware_context.cpp
)

set_tests_properties(HorizonTests
    PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 120
)
