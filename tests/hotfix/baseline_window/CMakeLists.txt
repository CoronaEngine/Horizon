find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(glfw3 3.4 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(volk CONFIG REQUIRED)

set(example_dir "${PROJECT_SOURCE_DIR}/examples")
set(baseline_dir "${example_dir}/example_baseline")
set(original_source "${baseline_dir}/baseline_hotfix_test.cpp")
set(patch_source "${CMAKE_CURRENT_BINARY_DIR}/patch/baseline_hotfix_test.cpp")

# CMake can regenerate during a hotfix build. Preserve the driver's live edit.
if(NOT EXISTS "${patch_source}")
    configure_file("${original_source}" "${patch_source}" COPYONLY)
endif()

add_library(horizon-baseline-live-patch MODULE EXCLUDE_FROM_ALL "${patch_source}")
target_include_directories(horizon-baseline-live-patch PRIVATE "${baseline_dir}")
target_compile_definitions(horizon-baseline-live-patch PRIVATE HORIZON_BASELINE_HOTFIX_WINDOW_TEST=1)
include(horizon_hotfix_manifest)
horizon_register_hotfix_module(horizon-baseline-live-patch)

add_executable(horizon-test-baseline-window main.cpp
    native_window.cpp "${original_source}")
target_include_directories(horizon-test-baseline-window PRIVATE
    "${example_dir}" "${PROJECT_SOURCE_DIR}/src/tests")
target_compile_definitions(horizon-test-baseline-window PRIVATE
    HORIZON_EXAMPLE_HOTFIX_ENABLED=1 HORIZON_BASELINE_HOTFIX_WINDOW_TEST=1
    HORIZON_TEST_EXAMPLES_DIR="${example_dir}")
target_link_libraries(horizon-test-baseline-window PRIVATE
    glfw glm::glm volk::volk horizon::stb horizon-hotfix horizon-hotfix-build-cmake)
if(WIN32)
    target_link_libraries(horizon-test-baseline-window PRIVATE horizon-hotfix-process-windows)
else()
    target_link_libraries(horizon-test-baseline-window PRIVATE horizon-hotfix-process-posix)
endif()
add_custom_command(TARGET horizon-test-baseline-window POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:horizon-test-baseline-window>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/baseline_vert.spv" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/baseline_frag.spv"
        "$<TARGET_FILE_DIR:horizon-test-baseline-window>/shaders"
    VERBATIM)

add_test(NAME horizon.hotfix.baseline_window COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../run_baseline_window.py"
    "$<TARGET_FILE:horizon-test-baseline-window>" "${CMAKE_BINARY_DIR}" "$<CONFIG>"
    "${patch_source}" "${original_source}" "${CMAKE_COMMAND}")
set_tests_properties(horizon.hotfix.baseline_window PROPERTIES
    LABELS "hotfix;integration;window" RESOURCE_LOCK horizon-baseline-window TIMEOUT 240)
