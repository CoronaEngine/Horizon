set(HORIZON_EXAMPLE_SOURCES
    main.cpp
    #example_1/example_1.cpp
    #example_2/example_2.cpp
    #example_3/example_3.cpp
)

set(HORIZON_EXAMPLE_HEADERS
    #example_1/example_1.hpp
    #example_2/example_2.hpp
    #example_3/example_3.hpp
)

add_executable(HorizonExamples ${HORIZON_EXAMPLE_SOURCES} ${HORIZON_EXAMPLE_HEADERS})

target_include_directories(HorizonExamples PRIVATE
    "${glfw_SOURCE_DIR}/include"
    "${stb_SOURCE_DIR}")

target_link_libraries(HorizonExamples PRIVATE
    glfw
    Horizon)

helicon_install_runtime_deps(HorizonExamples)
corona_copy_tbb_runtime_artifacts(HorizonExamples)

helicon_compile_shaders(HorizonExamples)
