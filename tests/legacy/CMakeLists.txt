add_executable(horizon-test-legacy test_legacy.cpp test_public_headers.cpp)
target_link_libraries(horizon-test-legacy PRIVATE Horizon)
target_include_directories(horizon-test-legacy PRIVATE
    "${PROJECT_SOURCE_DIR}/tests"
    "${PROJECT_SOURCE_DIR}/src"
)
if(MSVC)
    target_compile_options(horizon-test-legacy PRIVATE /utf-8)
endif()

add_test(NAME horizon-test-legacy COMMAND horizon-test-legacy)
add_dependencies(horizon-tests horizon-test-legacy)
helicon_install_runtime_deps(horizon-test-legacy)
helicon_compile_shaders(horizon-test-legacy
    DEPENDS "${PROJECT_SOURCE_DIR}/examples/Disney/disney_scene.slangh"
    RASTER_PAIRS
    Baseline/Slang/baseline.pipeline.hpp
    "${PROJECT_SOURCE_DIR}/examples/Baseline/Slang/baseline_vert.slang"
    "${PROJECT_SOURCE_DIR}/examples/Baseline/Slang/baseline_frag.slang"
    Disney/Slang/disney.pipeline.hpp
    "${PROJECT_SOURCE_DIR}/examples/Disney/Slang/disney_vert.slang"
    "${PROJECT_SOURCE_DIR}/examples/Disney/Slang/disney_frag.slang"
    Disney/Slang/disney_drawstress.pipeline.hpp
    "${PROJECT_SOURCE_DIR}/examples/Disney/Slang/disney_vert.slang"
    "${PROJECT_SOURCE_DIR}/examples/DrawStress/Slang/drawstress_frag.slang"
    MESH_PAIRS Baseline/Mesh/baseline.pipeline.hpp
    "${PROJECT_SOURCE_DIR}/examples/Baseline/Mesh/baseline_mesh.slang"
    "${PROJECT_SOURCE_DIR}/examples/Baseline/Slang/baseline_frag.slang"
    TASK_MESH_PAIRS Baseline/Mesh/baseline_task.pipeline.hpp
    mesh_task.slang
    "${PROJECT_SOURCE_DIR}/examples/Baseline/Mesh/baseline_mesh.slang"
    "${PROJECT_SOURCE_DIR}/examples/Baseline/Slang/baseline_frag.slang")
set_tests_properties(horizon-test-legacy PROPERTIES LABELS "legacy;cpu" TIMEOUT 30)
add_test(NAME horizon.legacy.gpu COMMAND horizon-test-legacy --gpu)
set_tests_properties(horizon.legacy.gpu PROPERTIES
    LABELS "legacy;gpu" TIMEOUT 90 SKIP_RETURN_CODE 77)
add_test(NAME horizon.mesh.gpu COMMAND horizon-test-legacy --mesh)
add_test(NAME horizon.task-mesh.gpu COMMAND horizon-test-legacy --task-mesh)
set_tests_properties(horizon.mesh.gpu horizon.task-mesh.gpu PROPERTIES LABELS "legacy;gpu" TIMEOUT 60 SKIP_RETURN_CODE 77)
