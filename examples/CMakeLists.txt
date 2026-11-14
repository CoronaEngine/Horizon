add_executable(HorizonExamples
    main.cpp
    common.cpp
    legacy_common.cpp
    example_baseline/28_model_loading.cpp
    example_disney_pbr/example_disney_pbr.cpp
    example_drawstress/example_drawstress.cpp
    example_edsl/example_edsl.cpp
    example_edsl_disney_pbr/example_edsl_disney_pbr.cpp
    example_edsl_drawstress/example_edsl_drawstress.cpp
    example_edsl_rsm/example_edsl_rsm.cpp
    example_edsl_shadowvolumes/example_edsl_shadowvolumes.cpp
    example_edsl_sponza/example_edsl_sponza.cpp
    example_edsl_ssr/example_edsl_ssr.cpp
    example_glsl/example_glsl.cpp
    example_rsm/example_rsm.cpp
    example_shadowvolumes/example_shadowvolumes.cpp
    example_sponza/example_sponza.cpp
    example_ssr/example_ssr.cpp
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
helicon_compile_shaders(HorizonExamples DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/raster_batch.slang")
