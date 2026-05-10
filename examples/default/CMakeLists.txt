set(DEFAULT_EXAMPLE_SOURCES
    main.cpp
    config.h
    cube_data.h
    signal_handler.h
    texture_test.h)

add_executable(default ${DEFAULT_EXAMPLE_SOURCES})

target_include_directories(default PRIVATE
    "${glfw_SOURCE_DIR}/include"
    "${stb_SOURCE_DIR}")

target_link_libraries(default PRIVATE
    glfw
    Horizon)

set_target_properties(default PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:default>")

helicon_install_runtime_deps(default)
corona_copy_tbb_runtime_artifacts(default)

helicon_compile_shaders(default)
