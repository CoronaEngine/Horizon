function(ocarina_add_test name)
    cmake_parse_arguments(_ocarina_test "GPU;BENCHMARK" "CATEGORY" "SOURCES;CASES" ${ARGN})
    set(_test_target ${name})
    if (_ocarina_test_CATEGORY)
        if (${name} MATCHES "^test-")
            string(REGEX REPLACE "^test-" "test-${_ocarina_test_CATEGORY}-" _test_target ${name})
        else ()
            set(_test_target "test-${_ocarina_test_CATEGORY}-${name}")
        endif ()
    endif ()
    add_executable(${_test_target} ${_ocarina_test_SOURCES})
    target_link_libraries(${_test_target} ocarina-native)
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
    if(NOT _ocarina_test_BENCHMARK)
        if(_ocarina_test_GPU)
            foreach(_backend cuda vulkan-compute)
                if(TARGET ocarina-backend-${_backend})
                    foreach(_codegen ast ir)
                      set(_cases ${_ocarina_test_CASES})
                      if(NOT _cases)
                          set(_cases default)
                      endif()
                      foreach(_case ${_cases})
                        set(_test_name "ocarina.${_test_target}.${_backend}.${_codegen}")
                        if(_case STREQUAL "default")
                            add_test(NAME ${_test_name} COMMAND $<TARGET_FILE:${_test_target}>)
                        else()
                            string(APPEND _test_name ".${_case}")
                            add_test(NAME ${_test_name} COMMAND $<TARGET_FILE:${_test_target}> --case ${_case})
                        endif()
                        set_tests_properties(${_test_name} PROPERTIES
                            WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_test_target}>"
                            ENVIRONMENT "OCARINA_TEST_BACKEND=${_backend};OCARINA_TEST_CODEGEN=${_codegen}"
                            LABELS "ocarina;${_backend};${_codegen}"
                            RUN_SERIAL TRUE TIMEOUT 300)
                      endforeach()
                    endforeach()
                endif()
            endforeach()
        else()
            add_test(NAME ocarina.${_test_target} COMMAND $<TARGET_FILE:${_test_target}>)
            set_tests_properties(ocarina.${_test_target} PROPERTIES
                WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_test_target}>"
                LABELS "ocarina" RUN_SERIAL TRUE TIMEOUT 120)
        endif()
    endif()
endfunction()

# Core type and utility tests.
ocarina_add_test(test-parsetype CATEGORY core SOURCES core/test_parse_type.cpp)
ocarina_add_test(test-precision-caps CATEGORY core SOURCES core/test_precision_caps.cpp)
ocarina_add_test(test-half GPU CATEGORY core SOURCES core/test_half.cpp)
ocarina_add_test(test-function-corrector CATEGORY core SOURCES core/test_function_corrector.cpp)
ocarina_add_test(test-trait CATEGORY core SOURCES core/test_trait.cpp)

# Codegen and shader interface tests.
ocarina_add_test(test-shader-argument-pack CATEGORY codegen SOURCES codegen/test_shader_argument_pack.cpp)
ocarina_add_test(test-shader-parameter-policy GPU CATEGORY codegen SOURCES codegen/test_shader_parameter_policy.cpp)
ocarina_add_test(test-ast-to-cuda-source-resolved-types CATEGORY codegen SOURCES
    codegen/test_ast_to_cuda_source_resolved_types.cpp)
ocarina_add_test(test-tensor-gemm-codegen CATEGORY codegen SOURCES codegen/test_tensor_gemm_codegen.cpp)

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
ocarina_add_test(test-backend-parity GPU CATEGORY runtime SOURCES runtime/test_backend_parity.cpp)
ocarina_add_test(test-render-parity GPU CATEGORY runtime SOURCES runtime/test_render_parity.cpp)
ocarina_add_test(test-codegen-parity-extended GPU CATEGORY runtime SOURCES runtime/test_codegen_parity_extended.cpp)
ocarina_add_test(test-runtime-parity-extended GPU CATEGORY runtime SOURCES runtime/test_runtime_parity_extended.cpp)
ocarina_add_test(test-compute-dispatch-parity GPU CATEGORY runtime SOURCES runtime/test_compute_dispatch_parity.cpp)
ocarina_add_test(test-compute-math-parity GPU CATEGORY runtime SOURCES runtime/test_compute_math_parity.cpp)
ocarina_add_test(test-compute-atomic-parity GPU CATEGORY runtime SOURCES runtime/test_compute_atomic_parity.cpp)
ocarina_add_test(test-storage-parity GPU CATEGORY runtime SOURCES runtime/test_storage_parity.cpp)
ocarina_add_test(test-stream-order GPU CATEGORY runtime SOURCES runtime/test_stream_order.cpp)
ocarina_add_test(test-host-transfer-order GPU CATEGORY runtime SOURCES runtime/test_host_transfer_order.cpp)
ocarina_add_test(test-accel-update GPU CATEGORY runtime SOURCES runtime/test_accel_update.cpp)
ocarina_add_test(test-launch-latency BENCHMARK CATEGORY runtime SOURCES runtime/test_launch_latency.cpp)
ocarina_add_test(test-backend-performance BENCHMARK CATEGORY runtime SOURCES runtime/test_backend_performance.cpp)
# Isolate capability regressions so one failure cannot hide later scenarios.
ocarina_add_test(test-codegen-capability-probe GPU CATEGORY runtime SOURCES runtime/test_codegen_capability_probe.cpp
    CASES inverse2 determinant2 callable-storage callable-alias fmod-mod ulong-makers)
ocarina_add_test(test-backend-capability-probe GPU CATEGORY runtime SOURCES runtime/test_backend_capability_probe.cpp
    CASES empty-accel fast-build mesh-rebuild same-batch-accel multistream-ray-params)
ocarina_add_test(test-resource-capability-probe GPU CATEGORY resources SOURCES resources/test_resource_capability_probe.cpp
    CASES texture-buffer-view-offset texture-narrow-channels texture-linear-fraction texture-uint-reinterpret)
ocarina_add_test(test-float32-policy-probe GPU CATEGORY runtime SOURCES runtime/test_float32_policy_probe.cpp
    CASES float32-subnormal hyperbolic-stability half-intermediate half-inverse)
if(TARGET ocarina-backend-cuda AND TARGET ocarina-backend-vulkan-compute)
    find_package(Python COMPONENTS Interpreter REQUIRED)
    add_test(NAME ocarina.capability-cross-backend
        COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_capability_audit.py"
            --runtime "$<TARGET_FILE_DIR:test-runtime-codegen-capability-probe>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/capability-comparison/$<CONFIG>")
    set_tests_properties(ocarina.capability-cross-backend PROPERTIES
        LABELS "ocarina;cuda;vulkan-compute;parity" RUN_SERIAL TRUE TIMEOUT 1200)
endif()
ocarina_add_test(test-atomic GPU CATEGORY runtime SOURCES runtime/test_atomic.cpp)
ocarina_add_test(test-tensor-gemm GPU CATEGORY runtime SOURCES runtime/test_tensor_gemm.cpp)

# Debug and diagnostic tests.
ocarina_add_test(test-printer GPU CATEGORY debug SOURCES debug/test_printer.cpp)
ocarina_add_test(test-printer-bytebuffer-debug GPU CATEGORY debug SOURCES debug/test_printer_bytebuffer_debug.cpp)
ocarina_add_test(test-printer-bindless-buffer-debug GPU CATEGORY debug SOURCES debug/test_printer_bindless_buffer_debug.cpp)

# IO tests.
ocarina_add_test(test-imageio BENCHMARK CATEGORY io SOURCES io/test_image_io.cpp)
