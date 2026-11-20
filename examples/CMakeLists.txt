add_executable(HorizonExamples
    main.cpp
    common.cpp
    legacy_common.cpp
    present_benchmark.cpp
    example_baseline/28_model_loading.cpp
    example_baseline_edsl/example_baseline_edsl.cpp
    example_baseline_slang/example_baseline_slang.cpp
    example_disney_edsl/example_disney_edsl.cpp
    example_disney_slang/example_disney_slang.cpp
    example_drawstress_edsl/example_drawstress_edsl.cpp
    example_drawstress_slang/example_drawstress_slang.cpp
)
find_program(TUTORIAL_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
set(baseline_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/example_baseline")
if(TUTORIAL_GLSLC)
    set(baseline_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/baseline_shaders")
    foreach(stage IN ITEMS vert frag)
        set(source "${CMAKE_CURRENT_SOURCE_DIR}/example_baseline/27_shader_depth.${stage}")
        set(output "${baseline_shader_dir}/${stage}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${baseline_shader_dir}"
            COMMAND "${TUTORIAL_GLSLC}" "${source}" -o "${output}"
            DEPENDS "${source}" VERBATIM)
        target_sources(HorizonExamples PRIVATE "${output}")
    endforeach()
endif()
set_property(SOURCE example_baseline/28_model_loading.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS "TUTORIAL_SHADER_DIR=\"${baseline_shader_dir}\"")
if(WIN32 AND HORIZON_BUILD_OCARINA)
    # Run beside the shared logging provider and Ocarina DLLs.
    set_target_properties(HorizonExamples PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
target_include_directories(HorizonExamples PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(HorizonExamples PRIVATE
    Horizon horizon::glm horizon::glfw horizon::stb horizon::tinyobjloader)
if(HORIZON_ENABLE_TRACY)
    target_link_libraries(HorizonExamples PRIVATE Tracy::TracyClient)
    target_compile_definitions(HorizonExamples PRIVATE HORIZON_TRACY_ENABLED=1)
endif()
helicon_install_runtime_deps(HorizonExamples)
helicon_compile_shaders(HorizonExamples DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/raster_batch.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/example_baseline_slang/baseline_draw.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/example_disney_slang/disney_shared.slang")

if(HORIZON_BUILD_TESTS)
    add_dependencies(horizon-tests HorizonExamples)
    add_test(NAME horizon.examples.list COMMAND HorizonExamples --list)
    foreach(mode IN ITEMS baseline baseline_edsl baseline_slang disney_edsl disney_slang drawstress_edsl drawstress_slang)
        add_test(NAME horizon.examples.${mode} COMMAND HorizonExamples ${mode})
        set_tests_properties(horizon.examples.${mode} PROPERTIES
            LABELS "examples;gpu"
            RESOURCE_LOCK horizon_example_window
            TIMEOUT 90)
        set_property(TEST horizon.examples.${mode} PROPERTY ENVIRONMENT
            "HORIZON_PRESENT_FRAMES=8" "HORIZON_BENCH_WARMUP=2"
            "HORIZON_BENCH_FRAMES=0" "HORIZON_BENCH_OFFSCREEN=0"
            "HORIZON_BENCH_HIDDEN=0" "HORIZON_CAPTURE_PATH=")
    endforeach()
endif()
