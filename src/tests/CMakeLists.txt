add_custom_target(ocarina-tests)

function(ocarina_add_test name)
    cmake_parse_arguments(_ocarina_test "GPU;BENCHMARK;LOCAL_TRACE" "CATEGORY" "SOURCES;CASES;ARGS" ${ARGN})
    set(_test_target ${name})
    if (_ocarina_test_CATEGORY)
        if (${name} MATCHES "^test-")
            string(REGEX REPLACE "^test-" "test-${_ocarina_test_CATEGORY}-" _test_target ${name})
        else ()
            set(_test_target "test-${_ocarina_test_CATEGORY}-${name}")
        endif ()
    endif ()
    add_executable(${_test_target} ${_ocarina_test_SOURCES})
    if(_ocarina_test_LOCAL_TRACE)
        add_dependencies(ocarina-trace-mode-tests ${_test_target})
        set(_test_prefix horizon.trace_mode)
        set(_test_label trace-mode-local)
    else()
        add_dependencies(ocarina-tests ${_test_target})
        set_property(GLOBAL APPEND PROPERTY HORIZON_OCARINA_TEST_TARGETS ${_test_target})
        set(_test_prefix ocarina)
        set(_test_label ocarina)
    endif()
    target_link_libraries(${_test_target} PRIVATE ocarina-native)
    set(_test_folder "ocarina/test")
    if (_ocarina_test_CATEGORY)
        set(_test_folder "${_test_folder}/${_ocarina_test_CATEGORY}")
    endif ()
    set_target_properties(${_test_target} PROPERTIES
            PUBLIC_HEADER "${HEADER_FILES}"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            FOLDER "${_test_folder}")
    if(_ocarina_test_BENCHMARK)
        set_property(TARGET ${_test_target} PROPERTY HORIZON_TEST_BENCHMARK true)
        if(_ocarina_test_CATEGORY STREQUAL "runtime")
            add_test(NAME ocarina.${_test_target}.benchmark
                COMMAND $<TARGET_FILE:${_test_target}> --ci ${_ocarina_test_ARGS})
        else()
            add_test(NAME ocarina.${_test_target}.benchmark COMMAND $<TARGET_FILE:${_test_target}>)
        endif()
        set_tests_properties(ocarina.${_test_target}.benchmark PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_test_target}>"
            LABELS "ocarina;benchmark" RUN_SERIAL TRUE TIMEOUT 300)
    else()
        set_property(TARGET ${_test_target} PROPERTY HORIZON_TEST_BENCHMARK false)
        if(_ocarina_test_GPU)
            if(TARGET ocarina-backend-vulkan-compute)
                set(_cases ${_ocarina_test_CASES})
                if(NOT _cases)
                    set(_cases default)
                endif()
                foreach(_case ${_cases})
                    set(_test_name "${_test_prefix}.${_test_target}.vulkan-compute")
                    if(_case STREQUAL "default")
                        add_test(NAME ${_test_name} COMMAND $<TARGET_FILE:${_test_target}>)
                    else()
                        string(APPEND _test_name ".${_case}")
                        add_test(NAME ${_test_name} COMMAND $<TARGET_FILE:${_test_target}> --case ${_case})
                    endif()
                    set_tests_properties(${_test_name} PROPERTIES
                        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_test_target}>"
                        LABELS "${_test_label};vulkan-compute"
                        RUN_SERIAL TRUE TIMEOUT 300)
                    if(_ocarina_test_LOCAL_TRACE)
                        set_tests_properties(${_test_name} PROPERTIES SKIP_RETURN_CODE 77)
                    endif()
                endforeach()
            endif()
        else()
            add_test(NAME ocarina.${_test_target} COMMAND $<TARGET_FILE:${_test_target}>)
            set_tests_properties(ocarina.${_test_target} PROPERTIES
                WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_test_target}>"
                LABELS "ocarina" RUN_SERIAL TRUE TIMEOUT 120)
        endif()
    endif()
endfunction()

# Core type and utility tests.
ocarina_add_test(test-allocator CATEGORY core SOURCES core/test_allocator.cpp)
add_test(NAME ocarina.test-core-allocator.failures
    COMMAND "${CMAKE_COMMAND}" "-DALLOCATOR_TEST=$<TARGET_FILE:test-core-allocator>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/core/check_allocator_failures.cmake")
set_tests_properties(ocarina.test-core-allocator.failures PROPERTIES
    LABELS "ocarina" RUN_SERIAL TRUE TIMEOUT 120)
ocarina_add_test(test-logging CATEGORY core SOURCES core/test_logging.cpp)
target_link_libraries(test-core-logging PRIVATE horizon-logging)
add_library(ocarina-platform-fixture MODULE core/platform_fixture.cpp)
set_target_properties(ocarina-platform-fixture PROPERTIES FOLDER "ocarina/test/core")
ocarina_add_test(test-platform CATEGORY core SOURCES core/test_platform.cpp)
target_compile_definitions(test-core-platform PRIVATE
    OC_PLATFORM_FIXTURE_PATH="$<TARGET_FILE:ocarina-platform-fixture>"
    OC_PLATFORM_BACKEND_FILENAME="$<TARGET_FILE_NAME:ocarina-backend-vulkan-compute>")
add_dependencies(test-core-platform ocarina-platform-fixture)
ocarina_add_test(test-parsetype CATEGORY core SOURCES core/test_parse_type.cpp)
add_test(NAME ocarina.test-core-parsetype.rejections
    COMMAND "${CMAKE_COMMAND}" "-DPARSE_TYPE_TEST=$<TARGET_FILE:test-core-parsetype>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/core/check_parse_type_rejections.cmake")
set_tests_properties(ocarina.test-core-parsetype.rejections PROPERTIES
    LABELS "ocarina" RUN_SERIAL TRUE TIMEOUT 60)
ocarina_add_test(test-precision-caps CATEGORY core SOURCES core/test_precision_caps.cpp)
ocarina_add_test(test-half GPU CATEGORY core SOURCES core/test_half.cpp)
ocarina_add_test(test-function-corrector CATEGORY core SOURCES
    core/test_function_corrector.cpp
    core/function_corrector_visitors.cpp
    core/function_corrector_calls.cpp
    core/function_corrector_parameters.cpp)
add_test(NAME ocarina.test-core-function-corrector.rejected-captures
    COMMAND "${CMAKE_COMMAND}" "-DCORRECTOR_TEST=$<TARGET_FILE:test-core-function-corrector>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/core/check_function_corrector_rejections.cmake")
set_tests_properties(ocarina.test-core-function-corrector.rejected-captures PROPERTIES
    LABELS "ocarina" RUN_SERIAL TRUE TIMEOUT 60)
ocarina_add_test(test-trait CATEGORY core SOURCES core/test_trait.cpp)

# Codegen and shader interface tests.
ocarina_add_test(test-trace-requirements CATEGORY codegen SOURCES
    codegen/test_trace_requirements.cpp
    ../backends/vulkan_compute/ast_to_slang_source.cpp)
target_link_libraries(test-codegen-trace-requirements PRIVATE ocarina-generator
    horizon::vulkan_headers vulkan-memory-allocator::vulkan-memory-allocator)
ocarina_add_test(test-shader-argument-pack CATEGORY codegen SOURCES codegen/test_shader_argument_pack.cpp)
ocarina_add_test(test-shader-parameter-policy GPU CATEGORY codegen SOURCES codegen/test_shader_parameter_policy.cpp)
ocarina_add_test(test-slang-conversions GPU CATEGORY codegen SOURCES codegen/test_slang_conversions.cpp)
target_link_libraries(test-codegen-slang-conversions PRIVATE slang::slang)
ocarina_add_test(test-slang-alias-diagnostics GPU CATEGORY codegen SOURCES codegen/test_slang_alias_diagnostics.cpp)
target_link_libraries(test-codegen-slang-alias-diagnostics PRIVATE slang::slang)
if(TEST ocarina.test-codegen-slang-alias-diagnostics.vulkan-compute)
    # Exercise backend and Slang builtin discovery from outside the executable directory.
    set_tests_properties(ocarina.test-codegen-slang-alias-diagnostics.vulkan-compute PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# Resource and buffer tests.
ocarina_add_test(test-host-bytebuffer CATEGORY resources SOURCES resources/test_host_byte_buffer.cpp)
ocarina_add_test(test-host-dynamic-buffer CATEGORY resources SOURCES resources/test_host_dynamic_buffer.cpp)
ocarina_add_test(test-dynamic-buffer GPU CATEGORY resources SOURCES resources/test_dynamic_buffer.cpp)
ocarina_add_test(test-dynamic-buffer-layout-codec CATEGORY resources SOURCES resources/test_dynamic_buffer_layout_codec.cpp)
ocarina_add_test(test-counted_buffer GPU CATEGORY resources SOURCES resources/test_counted_buffer.cpp)
ocarina_add_test(test-bindless-array GPU CATEGORY resources SOURCES resources/test_bindless_array.cpp)
ocarina_add_test(test-buffer-access-modes GPU CATEGORY resources SOURCES resources/test_buffer_access_modes.cpp)
ocarina_add_test(test-bytebuffer-access-modes GPU CATEGORY resources SOURCES resources/test_bytebuffer_access_modes.cpp)
ocarina_add_test(test-texture-access-modes GPU CATEGORY resources SOURCES resources/test_texture_access_modes.cpp)
ocarina_add_test(test-texture-readwrite GPU CATEGORY resources SOURCES resources/test_texture_readwrite.cpp)
ocarina_add_test(test-texture-parity-extended GPU CATEGORY resources SOURCES resources/test_texture_parity_extended.cpp)

# Math layout and algebra tests.
ocarina_add_test(test-soa GPU CATEGORY math SOURCES math/test_soa.cpp)
ocarina_add_test(test-matrix-inverse CATEGORY math SOURCES math/test_matrix_inverse.cpp)
ocarina_add_test(test-swizzle GPU CATEGORY math SOURCES math/test_swizzle.cpp)

# Runtime and backend-facing tests.
if(TARGET ocarina-backend-vulkan-compute)
    ocarina_add_test(test-native-callback GPU CATEGORY runtime SOURCES runtime/test_native_callback.cpp)
    target_link_libraries(test-runtime-native-callback PRIVATE horizon::vulkan_headers)
endif()
ocarina_add_test(test-shared-array GPU CATEGORY runtime SOURCES runtime/test_shared_array.cpp)
ocarina_add_test(test-slang-capabilities GPU CATEGORY runtime SOURCES runtime/test_slang_capabilities.cpp
    CASES compute)
target_link_libraries(test-runtime-slang-capabilities PRIVATE horizon::vulkan_headers)
ocarina_add_test(test-backend-parity GPU CATEGORY runtime SOURCES runtime/test_backend_parity.cpp)
ocarina_add_test(test-codegen-parity-extended GPU CATEGORY runtime SOURCES runtime/test_codegen_parity_extended.cpp)
ocarina_add_test(test-outline-equivalence GPU CATEGORY runtime SOURCES runtime/test_outline_equivalence.cpp
    CASES repeated-output returned-output control-flow-resources)
ocarina_add_test(test-runtime-parity-extended GPU CATEGORY runtime SOURCES runtime/test_runtime_parity_extended.cpp
    CASES compute)
ocarina_add_test(test-compute-dispatch-parity GPU CATEGORY runtime SOURCES runtime/test_compute_dispatch_parity.cpp
    CASES compute)
target_link_libraries(test-runtime-compute-dispatch-parity PRIVATE horizon::vulkan_headers)
ocarina_add_test(test-compute-math-parity GPU CATEGORY runtime SOURCES runtime/test_compute_math_parity.cpp)
ocarina_add_test(test-compute-atomic-parity GPU CATEGORY runtime SOURCES runtime/test_compute_atomic_parity.cpp
    CASES compute int64)
if(TARGET ocarina-backend-vulkan-compute)
    set_tests_properties(ocarina.test-runtime-compute-atomic-parity.vulkan-compute.int64 PROPERTIES
        LABELS "ocarina;vulkan-compute;int64-atomics" SKIP_RETURN_CODE 77)
endif()
ocarina_add_test(test-storage-parity GPU CATEGORY runtime SOURCES runtime/test_storage_parity.cpp)
ocarina_add_test(test-stream-order GPU CATEGORY runtime SOURCES runtime/test_stream_order.cpp)
ocarina_add_test(test-host-transfer-order GPU CATEGORY runtime SOURCES runtime/test_host_transfer_order.cpp)
ocarina_add_test(test-launch-latency BENCHMARK CATEGORY runtime SOURCES runtime/test_launch_latency.cpp)
ocarina_add_test(test-backend-performance BENCHMARK CATEGORY runtime SOURCES runtime/test_backend_performance.cpp
    ARGS --no-raytracing)
# Isolate capability regressions so one failure cannot hide later scenarios.
ocarina_add_test(test-codegen-capability-probe GPU CATEGORY runtime SOURCES runtime/test_codegen_capability_probe.cpp
    CASES inverse2 determinant2 callable-storage callable-alias fmod-mod ulong-makers vector-narrow)
ocarina_add_test(test-resource-capability-probe GPU CATEGORY resources SOURCES resources/test_resource_capability_probe.cpp
    CASES texture-buffer-view-offset texture-narrow-channels texture-linear-fraction texture-uint-reinterpret)
ocarina_add_test(test-float32-policy-probe GPU CATEGORY runtime SOURCES runtime/test_float32_policy_probe.cpp
    CASES float32-subnormal hyperbolic-stability half-intermediate half-inverse)
if(TARGET ocarina-backend-vulkan-compute)
    find_package(Python 3.11 COMPONENTS Interpreter REQUIRED)
    add_test(NAME ocarina.capability-audit
        COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_capability_audit.py"
            --runtime "$<TARGET_FILE_DIR:test-runtime-codegen-capability-probe>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/capability-audit/$<CONFIG>")
    set_tests_properties(ocarina.capability-audit PROPERTIES
        LABELS "ocarina;vulkan-compute;capability" RUN_SERIAL TRUE TIMEOUT 1200)
endif()
ocarina_add_test(test-atomic GPU CATEGORY runtime SOURCES runtime/test_atomic.cpp)
ocarina_add_test(test-tensor-gemm GPU CATEGORY runtime SOURCES runtime/test_tensor_gemm.cpp)

# Debug and diagnostic tests.
ocarina_add_test(test-printer GPU CATEGORY debug SOURCES debug/test_printer.cpp)
ocarina_add_test(test-printer-bytebuffer-debug GPU CATEGORY debug SOURCES debug/test_printer_bytebuffer_debug.cpp)
ocarina_add_test(test-printer-bindless-buffer-debug GPU CATEGORY debug SOURCES debug/test_printer_bindless_buffer_debug.cpp)

# IO tests.
ocarina_add_test(test-imageio BENCHMARK CATEGORY io SOURCES io/test_image_io.cpp)

# Use the same CMake inventory for compilation and per-platform execution proof.
get_property(_ci_targets GLOBAL PROPERTY HORIZON_OCARINA_TEST_TARGETS)
set(_ci_manifest_entries)
foreach(_target IN LISTS _ci_targets)
    get_property(_benchmark TARGET ${_target} PROPERTY HORIZON_TEST_BENCHMARK)
    list(APPEND _ci_manifest_entries
        "{\"target\":\"${_target}\",\"path\":\"$<TARGET_FILE:${_target}>\",\"benchmark\":${_benchmark}}")
endforeach()
list(JOIN _ci_manifest_entries ",\n" _ci_manifest)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ci/$<CONFIG>/targets.json"
    CONTENT "[\n${_ci_manifest}\n]\n")

if(HORIZON_BUILD_TRACE_MODE_TESTS)
    add_subdirectory(trace_modes)
    add_dependencies(ocarina-tests ocarina-trace-mode-tests)
endif()

if(HORIZON_BUILD_WARP32_TESTS AND TARGET ocarina-backend-vulkan-compute)
    foreach(test IN ITEMS slang-capabilities compute-dispatch-parity)
        add_test(NAME horizon.warp32.${test}
            COMMAND $<TARGET_FILE:test-runtime-${test}> --case warp32)
        set_tests_properties(horizon.warp32.${test} PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:test-runtime-${test}>"
            LABELS "warp32-local" RUN_SERIAL TRUE TIMEOUT 300 SKIP_RETURN_CODE 77)
    endforeach()
endif()
