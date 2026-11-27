add_executable(HorizonExamples
    main.cpp
    Baseline/Tutorial/28_model_loading.cpp
    Baseline/EDSL/example_baseline_edsl.cpp
    Baseline/Slang/BaselineSlang.h
    Baseline/Slang/example_baseline_slang.cpp
    Baseline/Mesh/example_baseline_mesh.cpp
    Baseline/Ocarina/example_baseline_ocarina.cpp
    Disney/EDSL/example_disney_edsl.cpp
    Disney/Slang/DisneySlang.h
    Disney/Slang/example_disney_slang.cpp
    Disney/Mesh/example_disney_mesh.cpp
    Disney/Ocarina/example_disney_ocarina.cpp
    DrawStress/EDSL/example_drawstress_edsl.cpp
    DrawStress/Slang/DrawStressSlang.h
    DrawStress/Slang/example_drawstress_slang.cpp
    DrawStress/Mesh/example_drawstress_mesh.cpp
    DrawStress/Ocarina/example_drawstress_ocarina.cpp
    Baseline/RayTrace/example_baseline_raytrace.cpp
    Disney/RayTrace/example_disney_raytrace.cpp
    DrawStress/RayTrace/example_drawstress_raytrace.cpp
    Feature/example_edsl_feature.cpp
)
find_program(TUTORIAL_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
set(baseline_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/Baseline/Tutorial")
if(TUTORIAL_GLSLC)
    set(baseline_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/baseline_shaders")
    foreach(stage IN ITEMS vert frag)
        set(source "${CMAKE_CURRENT_SOURCE_DIR}/Baseline/Tutorial/27_shader_depth.${stage}")
        set(output "${baseline_shader_dir}/${stage}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${baseline_shader_dir}"
            COMMAND "${TUTORIAL_GLSLC}" "${source}" -o "${output}"
            DEPENDS "${source}" VERBATIM)
        target_sources(HorizonExamples PRIVATE "${output}")
    endforeach()
endif()
set_property(SOURCE Baseline/Tutorial/28_model_loading.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS
        "TUTORIAL_ASSET_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Baseline/assets\""
        "TUTORIAL_SHADER_DIR=\"${baseline_shader_dir}\"")
# Run beside the shared logging provider and Ocarina backends.
set_target_properties(HorizonExamples PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(HorizonExamples PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(HorizonExamples PRIVATE
    Horizon ocarina-native horizon::ktm horizon::glm horizon::glfw horizon::stb horizon::tinyobjloader)
helicon_install_runtime_deps(HorizonExamples)
helicon_compile_shaders(HorizonExamples
    RASTER_PAIRS
    Baseline/Slang/baseline.pipeline.hpp Baseline/Slang/baseline_vert.slang Baseline/Slang/baseline_frag.slang
    Disney/Slang/disney.pipeline.hpp Disney/Slang/disney_vert.slang Disney/Slang/disney_frag.slang
    DrawStress/Slang/drawstress.pipeline.hpp DrawStress/Slang/drawstress_vert.slang DrawStress/Slang/drawstress_frag.slang
    RAY_TRACE_PIPELINES
    Baseline/RayTrace/baseline.pipeline.hpp Baseline/RayTrace/baseline_raygen.slang Baseline/RayTrace/baseline_miss.slang Baseline/RayTrace/baseline_closest_hit.slang
    Disney/RayTrace/disney.pipeline.hpp Disney/RayTrace/disney_raygen.slang Disney/RayTrace/disney_miss.slang Disney/RayTrace/disney_closest_hit.slang
    DrawStress/RayTrace/drawstress.pipeline.hpp DrawStress/RayTrace/drawstress_raygen.slang DrawStress/RayTrace/drawstress_miss.slang DrawStress/RayTrace/drawstress_closest_hit.slang
    MESH_PAIRS
    Baseline/Mesh/baseline.pipeline.hpp Baseline/Mesh/baseline_mesh.slang Baseline/Slang/baseline_frag.slang
    Disney/Mesh/disney.pipeline.hpp Disney/Mesh/disney_mesh.slang Disney/Slang/disney_frag.slang
    DrawStress/Mesh/drawstress.pipeline.hpp DrawStress/Mesh/drawstress_mesh.slang DrawStress/Slang/drawstress_frag.slang)

if(HORIZON_BUILD_TESTS)
    add_dependencies(horizon-tests HorizonExamples)
endif()
