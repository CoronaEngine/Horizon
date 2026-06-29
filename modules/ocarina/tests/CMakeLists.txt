function(ocarina_add_test name)
    cmake_parse_arguments(TEST "" "CATEGORY" "SOURCES" ${ARGN})
    set(_test_target ${name})
    if (TEST_CATEGORY)
        if (${name} MATCHES "^test-")
            string(REGEX REPLACE "^test-" "test-${TEST_CATEGORY}-" _test_target ${name})
        else ()
            set(_test_target "test-${TEST_CATEGORY}-${name}")
        endif ()
    endif ()
    add_executable(${_test_target} ${TEST_SOURCES})
    target_link_libraries(${_test_target} ocarina-native)
    set(_test_folder "ocarina/test")
    if (TEST_CATEGORY)
        set(_test_folder "${_test_folder}/${TEST_CATEGORY}")
    endif ()
    set_target_properties(${_test_target} PROPERTIES
            PUBLIC_HEADER "${HEADER_FILES}"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            FOLDER "${_test_folder}")
endfunction()

# Core type and utility tests.
ocarina_add_test(test-parsetype CATEGORY core SOURCES core/test_parse_type.cpp)
ocarina_add_test(test-precision-caps CATEGORY core SOURCES core/test_precision_caps.cpp)
ocarina_add_test(test-half CATEGORY core SOURCES core/test_half.cpp)
ocarina_add_test(test-function-corrector CATEGORY core SOURCES core/test_function_corrector.cpp)
ocarina_add_test(test-trait CATEGORY core SOURCES core/test_trait.cpp)

# Codegen and shader interface tests.
ocarina_add_test(test-shader-argument-pack CATEGORY codegen SOURCES codegen/test_shader_argument_pack.cpp)
ocarina_add_test(test-shader-parameter-policy CATEGORY codegen SOURCES codegen/test_shader_parameter_policy.cpp)
ocarina_add_test(test-ast-to-cuda-source-resolved-types CATEGORY codegen SOURCES
    codegen/test_ast_to_cuda_source_resolved_types.cpp)
ocarina_add_test(test-tensor-gemm-codegen CATEGORY codegen SOURCES codegen/test_tensor_gemm_codegen.cpp)

# Resource and buffer tests.
ocarina_add_test(test-host-bytebuffer CATEGORY resources SOURCES resources/test_host_byte_buffer.cpp)
ocarina_add_test(test-host-dynamic-buffer CATEGORY resources SOURCES resources/test_host_dynamic_buffer.cpp)
ocarina_add_test(test-dynamic-buffer CATEGORY resources SOURCES resources/test_dynamic_buffer.cpp)
ocarina_add_test(test-dynamic-buffer-layout-codec CATEGORY resources SOURCES resources/test_dynamic_buffer_layout_codec.cpp)
ocarina_add_test(test-counted_buffer CATEGORY resources SOURCES resources/test_counted_buffer.cpp)
ocarina_add_test(test-bindless-array CATEGORY resources SOURCES resources/test_bindless_array.cpp)
ocarina_add_test(test-buffer-access-modes CATEGORY resources SOURCES resources/test_buffer_access_modes.cpp)
ocarina_add_test(test-bytebuffer-access-modes CATEGORY resources SOURCES resources/test_bytebuffer_access_modes.cpp)
ocarina_add_test(test-texture-access-modes CATEGORY resources SOURCES resources/test_texture_access_modes.cpp)
ocarina_add_test(test-texture-readwrite CATEGORY resources SOURCES resources/test_texture_readwrite.cpp)

# Math layout and algebra tests.
ocarina_add_test(test-soa CATEGORY math SOURCES math/test_soa.cpp)
ocarina_add_test(test-matrix-inverse CATEGORY math SOURCES math/test_matrix_inverse.cpp)
ocarina_add_test(test-swizzle CATEGORY math SOURCES math/test_swizzle.cpp)

# Runtime and backend-facing tests.
ocarina_add_test(test-accel-update CATEGORY runtime SOURCES runtime/test_accel_update.cpp)
ocarina_add_test(test-launch-latency CATEGORY runtime SOURCES runtime/test_launch_latency.cpp)
ocarina_add_test(test-atomic CATEGORY runtime SOURCES runtime/test_atomic.cpp)
ocarina_add_test(test-tensor-gemm CATEGORY runtime SOURCES runtime/test_tensor_gemm.cpp)

# Debug and diagnostic tests.
ocarina_add_test(test-printer CATEGORY debug SOURCES debug/test_printer.cpp)
ocarina_add_test(test-printer-bytebuffer-debug CATEGORY debug SOURCES debug/test_printer_bytebuffer_debug.cpp)
ocarina_add_test(test-printer-bindless-buffer-debug CATEGORY debug SOURCES debug/test_printer_bindless_buffer_debug.cpp)

# IO tests.
ocarina_add_test(test-imageio CATEGORY io SOURCES io/test_image_io.cpp)
