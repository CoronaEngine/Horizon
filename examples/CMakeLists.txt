set(HORIZON_EXAMPLE_SOURCES
    main.cpp
    common.cpp
    example_baseline/example_baseline.cpp
    example_glsl/example_glsl.cpp
    example_edsl/example_edsl.cpp
    example_default/example_default.cpp
)

set(HORIZON_EXAMPLE_HEADERS
    common.h
    example_baseline/example_baseline.h
    example_glsl/example_glsl.h
    example_edsl/example_edsl.h
    example_default/example_default.h
)

add_executable(HorizonExamples ${HORIZON_EXAMPLE_SOURCES} ${HORIZON_EXAMPLE_HEADERS})

target_include_directories(HorizonExamples PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${glfw_SOURCE_DIR}/include"
    "${stb_SOURCE_DIR}"
    "${tinyobjloader_SOURCE_DIR}"
)

target_link_libraries(HorizonExamples PRIVATE
    glm
    glfw
    Horizon)

find_program(GLSLANG_VALIDATOR_EXECUTABLE
    NAMES glslangValidator glslangValidator.exe
    HINTS "$ENV{VULKAN_SDK}/Bin"
)

if(NOT GLSLANG_VALIDATOR_EXECUTABLE)
    message(FATAL_ERROR "glslangValidator not found. Install the Vulkan SDK or add glslangValidator to PATH.")
endif()

add_custom_command(TARGET HorizonExamples POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HorizonExamples>/shaders"
    COMMAND "${GLSLANG_VALIDATOR_EXECUTABLE}" -V -S vert
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/baseline_vert.glsl"
        -o "$<TARGET_FILE_DIR:HorizonExamples>/shaders/baseline_vert.spv"
    COMMAND "${GLSLANG_VALIDATOR_EXECUTABLE}" -V -S frag
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/baseline_frag.glsl"
        -o "$<TARGET_FILE_DIR:HorizonExamples>/shaders/baseline_frag.spv"
    COMMENT "Compiling baseline shaders"
    VERBATIM
)

helicon_install_runtime_deps(HorizonExamples)
corona_copy_tbb_runtime_artifacts(HorizonExamples)

helicon_compile_shaders(HorizonExamples)
