add_executable(HorizonExamples
    main.cpp
    common.cpp
    legacy_common.cpp
    example_assao/example_assao.cpp
    example_baseline/indirect_baseline.cpp
    example_bump/example_bump.cpp
    example_deferred/example_deferred.cpp
    example_disney_pbr/example_disney_pbr.cpp
    example_drawstress/example_drawstress.cpp
    example_edsl/example_edsl.cpp
    example_edsl_compile/example_edsl_compile.cpp
    example_edsl_compile_comparison/example_edsl_compile_comparison.cpp
    example_edsl_disney_pbr/example_edsl_disney_pbr.cpp
    example_edsl_drawstress/example_edsl_drawstress.cpp
    example_edsl_ibl/example_edsl_ibl.cpp
    example_edsl_rsm/example_edsl_rsm.cpp
    example_edsl_shadowmaps/example_edsl_shadowmaps.cpp
    example_edsl_shadowvolumes/example_edsl_shadowvolumes.cpp
    example_edsl_sky/example_edsl_sky.cpp
    example_edsl_sponza/example_edsl_sponza.cpp
    example_edsl_ssr/example_edsl_ssr.cpp
    example_glsl/example_glsl.cpp
    example_gpudrivenrendering/example_gpudrivenrendering.cpp
    example_ibl/example_ibl.cpp
    example_raymarch/example_raymarch.cpp
    example_rsm/example_rsm.cpp
    example_shadowmaps/example_shadowmaps.cpp
    example_shadowvolumes/example_shadowvolumes.cpp
    example_sky/example_sky.cpp
    example_sponza/example_sponza.cpp
    example_ssr/example_ssr.cpp
)
if(WIN32 AND HORIZON_BUILD_OCARINA)
    # Run beside the shared logging provider and the Ocarina/hotfix DLLs.
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
