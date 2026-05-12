set(HORIZON_EXAMPLE_SOURCES
    main.cpp
)

set(HORIZON_EXAMPLE_HEADERS
    common/config.hpp
    common/cube_data.hpp
    common/signal_handler.hpp
    common/texture_utils.hpp
    default/default_test.hpp
)

add_executable(HorizonExamples ${HORIZON_EXAMPLE_SOURCES} ${HORIZON_EXAMPLE_HEADERS})

target_include_directories(HorizonExamples PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${glfw_SOURCE_DIR}/include"
    "${stb_SOURCE_DIR}")

target_link_libraries(HorizonExamples PRIVATE
    glfw
    Horizon)

helicon_install_runtime_deps(HorizonExamples)
corona_copy_tbb_runtime_artifacts(HorizonExamples)

helicon_compile_shaders(HorizonExamples)
