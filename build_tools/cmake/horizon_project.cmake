# Included in the root source-directory scope.
include(horizon_msvc_build_environment)
horizon_msvc_build_environment()

include(horizon_options)
include(horizon_compile_config)
include(horizon_dependencies)

include(horizon_core_dependencies)
include(horizon_image_dependencies)

if(HORIZON_BUILD_ENGINE)
    include(horizon_engine_dependencies)
    include(horizon_slang)
    include(horizon_tracy)
    include(horizon_runtime_deps)

    # ======================== Shader 自动编译模块 ========================
    include(HeliconShaderCompile)
endif()

add_subdirectory(src)

if(HORIZON_BUILD_TOOLS OR HORIZON_BUILD_EXAMPLES)
    add_subdirectory(tools)
endif()

if(HORIZON_BUILD_OCARINA_TESTS)
    enable_testing()
endif()

if(HORIZON_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

if(HORIZON_BUILD_EXAMPLES)
    include(horizon_example_dependencies)
    add_subdirectory(examples)
endif()

if (MSVC)
    add_compile_options(/Zi)
    add_link_options(/INCREMENTAL:NO)

    if(TARGET Helicon)
        target_compile_options(Helicon PUBLIC /utf-8)
    endif()
endif()

include(horizon_ocarina)

if(HORIZON_BUILD_VISION_HOTFIX)
    if(NOT TARGET ocarina OR NOT TARGET ocarina-include)
        message(FATAL_ERROR "HORIZON_BUILD_VISION_HOTFIX requires HORIZON_BUILD_OCARINA=ON and available ocarina targets")
    endif()
    target_include_directories(ocarina-include INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_subdirectory(src/hotfix)
    target_link_libraries(ocarina-include INTERFACE horizon-hotfix)

    if(TARGET HorizonExamples)
        target_link_libraries(HorizonExamples PRIVATE horizon-hotfix-all)
    endif()
endif()
