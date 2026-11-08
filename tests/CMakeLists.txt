add_custom_target(horizon-tests)

# Combined builds use the same shared Quill provider as the Ocarina executables.
if(WIN32 AND HORIZON_BUILD_OCARINA)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

add_subdirectory(core)
add_subdirectory(hotfix)

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
