set(HORIZON_EXAMPLE_SOURCES
    main.cpp
    common.cpp
    imgui_horizon.cpp
    example_baseline/example_baseline.cpp
    example_glsl/example_glsl.cpp
    example_edsl/example_edsl.cpp
    example_default/example_default.cpp
    example_branch_pruning/example_branch_pruning.cpp
    example_ibl/example_ibl.cpp
    example_drawstress/example_drawstress.cpp
)

if(HORIZON_BUILD_VISION_HOTFIX)
    list(APPEND HORIZON_EXAMPLE_SOURCES
        example_baseline/baseline_hotfix_test.cpp)
endif()

set(HORIZON_EXAMPLE_HEADERS
    common.h
    imgui_horizon.h
    example_baseline/example_baseline.h
    example_glsl/example_glsl.h
    example_edsl/example_edsl.h
    example_default/example_default.h
    example_branch_pruning/example_branch_pruning.h
    example_ibl/example_ibl.h
    example_drawstress/example_drawstress.h
)

if(HORIZON_BUILD_VISION_HOTFIX)
    list(APPEND HORIZON_EXAMPLE_HEADERS
        example_baseline/baseline_hotfix_test.h)
endif()

add_executable(HorizonExamples ${HORIZON_EXAMPLE_SOURCES} ${HORIZON_EXAMPLE_HEADERS})

if(HORIZON_BUILD_VISION_HOTFIX)
    target_compile_definitions(HorizonExamples PRIVATE
        HORIZON_EXAMPLE_HOTFIX_ENABLED=1)

    horizon_install_runtime_deps(HorizonExamples horizon-hotfix-all)
endif()

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
    horizon_imgui
    Horizon)

target_compile_definitions(HorizonExamples PRIVATE
    HORIZON_IMGUI_RENDER_ENABLED=$<BOOL:${HORIZON_ENABLE_IMGUI_RENDER}>)

add_custom_command(TARGET HorizonExamples POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:HorizonExamples>/shaders"
    COMMENT "Copying example shaders to HorizonExamples output directory"
    VERBATIM)

helicon_install_runtime_deps(HorizonExamples)
if(COMMAND corona_copy_tbb_runtime_artifacts)
    corona_copy_tbb_runtime_artifacts(HorizonExamples)
endif()
helicon_compile_shaders(HorizonExamples)
