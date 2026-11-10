add_custom_target(horizon-tests)

# Combined builds use the same shared Quill provider as the Ocarina executables.
if(WIN32 AND HORIZON_BUILD_OCARINA)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

add_subdirectory(core)
add_subdirectory(hotfix)

# Keep the checks also used by CI available from CLion's All CTest runner.
find_package(Python COMPONENTS Interpreter REQUIRED)
add_test(NAME horizon.python.build_tools
    COMMAND "${Python_EXECUTABLE}" -B -m unittest discover
        -s "${HORIZON_BUILD_TOOLS_DIR}/tests" -p "test_*.py")
add_test(NAME horizon.python.capability_audit
    COMMAND "${Python_EXECUTABLE}" -B -m unittest discover
        -s "${PROJECT_SOURCE_DIR}/src/tests" -p "test_*.py")
add_test(NAME horizon.generated.tuple
    COMMAND "${Python_EXECUTABLE}" -B
        "${PROJECT_SOURCE_DIR}/src/core/generate_tuple.py" --check)
set_tests_properties(horizon.python.build_tools horizon.python.capability_audit
    horizon.generated.tuple PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    LABELS "horizon;cpu;python"
    TIMEOUT 120)
# Some orchestration checks locate build tools through PATH. Reuse the tools
# selected by CMake so an IDE launched outside an activated shell tests them too.
set(_horizon_test_build_tool_path)
if(CMAKE_MAKE_PROGRAM)
    get_filename_component(_horizon_test_make_dir "${CMAKE_MAKE_PROGRAM}" DIRECTORY)
    list(APPEND _horizon_test_build_tool_path
        "PATH=path_list_prepend:${_horizon_test_make_dir}")
endif()
get_filename_component(_horizon_test_cmake_dir "${CMAKE_COMMAND}" DIRECTORY)
list(APPEND _horizon_test_build_tool_path
    "PATH=path_list_prepend:${_horizon_test_cmake_dir}")
set_tests_properties(horizon.python.build_tools PROPERTIES
    ENVIRONMENT_MODIFICATION "${_horizon_test_build_tool_path}")

if(HORIZON_BUILD_ENGINE AND HORIZON_USE_LEGACY_BACKEND)
    add_subdirectory(legacy)
endif()

set(_quill_logging_boundary_build_dir
    "${CMAKE_CURRENT_BINARY_DIR}/quill-logging-boundary")
add_test(
    NAME horizon.architecture.quill_logging_boundary
    COMMAND ${CMAKE_COMMAND} -E env HORIZON_DEV_BOOTSTRAP_ACTIVE=1
            ${CMAKE_COMMAND}
            -S ${PROJECT_SOURCE_DIR}
            -B ${_quill_logging_boundary_build_dir}
            -G "${CMAKE_GENERATOR}"
            -DHORIZON_DEV_CONFIGURATION=${HORIZON_DEV_CONFIGURATION}
            -DHORIZON_DEV_TARGET_FAMILY=${HORIZON_DEV_TARGET_FAMILY}
            -DHORIZON_BUILD_ENGINE=OFF
            -DHORIZON_BUILD_TESTS=OFF
            -DHORIZON_BUILD_EXAMPLES=OFF
            -DHORIZON_BUILD_TOOLS=OFF
            -DHORIZON_BUILD_OCARINA=OFF
            -DHORIZON_BUILD_OCARINA_TESTS=OFF
            -DCMAKE_DISABLE_FIND_PACKAGE_spdlog=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_xxHash=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_stb=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_tinyexr=TRUE
)
