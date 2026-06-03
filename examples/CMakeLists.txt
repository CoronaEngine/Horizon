set(HORIZON_EXAMPLE_SOURCES
    main.cpp
    #common.cpp
    #example_baseline/example_baseline.cpp
    #example_glsl/example_glsl.cpp
    #example_edsl/example_edsl.cpp
    #example_default/example_default.cpp
)

set(HORIZON_EXAMPLE_HEADERS
    #common.h
    #example_baseline/example_baseline.h
    #example_glsl/example_glsl.h
    #example_edsl/example_edsl.h
    #example_default/example_default.h
)

add_executable(HorizonExamples ${HORIZON_EXAMPLE_SOURCES} ${HORIZON_EXAMPLE_HEADERS})

target_include_directories(HorizonExamples PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${glfw_SOURCE_DIR}/include"
    "${stb_SOURCE_DIR}"
    "${tinyobjloader_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(HorizonExamples PRIVATE
    glm
    glfw
    Horizon)

set(HORIZON_EXAMPLE_SPIRV_SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/baseline_vert.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/baseline_frag.spv"
)

add_custom_command(TARGET HorizonExamples POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HorizonExamples>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HORIZON_EXAMPLE_SPIRV_SHADERS}
        "$<TARGET_FILE_DIR:HorizonExamples>/shaders"
    COMMENT "Copying baseline shaders"
    VERBATIM
)

helicon_install_runtime_deps(HorizonExamples)
corona_copy_tbb_runtime_artifacts(HorizonExamples)
helicon_compile_shaders(HorizonExamples)
