set(HORIZON_EXAMPLE_SOURCES
    main.cpp
    example_env/example_env.cpp
    common.cpp
    example_default/example_default.cpp
    example_baseline/example_baseline_tutorial.cpp
    example_baseline/example_baseline_glsl.cpp
)

set(HORIZON_EXAMPLE_HEADERS
    example_env/example_env.h
    example_default/example_default.h
)

add_executable(HorizonExamples ${HORIZON_EXAMPLE_SOURCES})

target_include_directories(HorizonExamples PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${glfw_SOURCE_DIR}/include"
    "${stb_SOURCE_DIR}"
    "${tinyobjloader_SOURCE_DIR}"
)

target_link_libraries(HorizonExamples PRIVATE
    glfw
    Horizon)

helicon_install_runtime_deps(HorizonExamples)
corona_copy_tbb_runtime_artifacts(HorizonExamples)

helicon_compile_shaders(HorizonExamples)
