add_executable(test-trace-modes EXCLUDE_FROM_ALL test_trace_modes.cpp)
target_link_libraries(test-trace-modes PRIVATE ocarina-native horizon::vulkan_headers)
add_dependencies(test-trace-modes ocarina-backend-vulkan-compute)
add_custom_target(ocarina-trace-mode-tests DEPENDS test-trace-modes test-runtime-backend-capability-probe)
set_target_properties(test-trace-modes PROPERTIES FOLDER "ocarina/test/trace_modes")

function(trace_mode_test case)
    set(name "horizon.trace_mode.${case}")
    add_test(NAME "${name}" COMMAND ${ARGN})
    set_tests_properties("${name}" PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:test-trace-modes>"
        LABELS "trace-mode-local" RUN_SERIAL TRUE TIMEOUT 300 SKIP_RETURN_CODE 77
        SKIP_REGULAR_EXPRESSION "TRACE_MODE_UNAVAILABLE")
endfunction()

foreach(case compute auto query pipeline concurrent query-images pipeline-images)
    trace_mode_test("${case}" $<TARGET_FILE:test-trace-modes> "${case}")
endforeach()
foreach(mode query pipeline)
    foreach(case empty-accel fast-build mesh-rebuild same-batch-accel multistream-ray-params)
        trace_mode_test("${mode}.${case}" $<TARGET_FILE:test-runtime-backend-capability-probe> --case "${case}" "${mode}")
    endforeach()
    trace_mode_test("${mode}.cache"
        "${CMAKE_COMMAND}" "-DTEST=$<TARGET_FILE:test-trace-modes>" "-DMODE=${mode}"
        "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/cache-$<CONFIG>" "-DPYTHON=${Python_EXECUTABLE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_trace_mode_cache.cmake")
endforeach()
trace_mode_test(rejection-wrapper "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_trace_rejection_wrapper.py"
    "${CMAKE_COMMAND}")
foreach(reason workgroup warp unknown limit-axis limit-product unavailable-query unavailable-pipeline)
    trace_mode_test("reject.${reason}"
        "${CMAKE_COMMAND}" "-DTEST=$<TARGET_FILE:test-trace-modes>" "-DREASON=${reason}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_trace_mode_rejections.cmake")
endforeach()
trace_mode_test(performance
    "${CMAKE_COMMAND}" "-DTEST=$<TARGET_FILE:test-trace-modes>" "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/performance-$<CONFIG>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_trace_mode_performance.cmake")
