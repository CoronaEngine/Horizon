foreach(contract process build)
    set(test_target horizon-test-hotfix-${contract}-contract)
    add_executable(${test_target} test_${contract}_contract.cpp)
    target_link_libraries(${test_target} PRIVATE horizon-hotfix-api)
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /utf-8)
    endif()
    add_dependencies(horizon-tests ${test_target})
    add_test(NAME horizon.hotfix.${contract}_contract COMMAND ${test_target})
    set_tests_properties(horizon.hotfix.${contract}_contract PROPERTIES
        LABELS "hotfix;contracts")
endforeach()

if(WIN32)
    add_executable(horizon-test-hotfix-process-child windows_process_child.cpp)
    set_target_properties(horizon-test-hotfix-process-child PROPERTIES
        OUTPUT_NAME "horizon hotfix process child")

    add_executable(horizon-test-hotfix-windows-process-runner test_windows_process_runner.cpp)
    target_link_libraries(horizon-test-hotfix-windows-process-runner
        PRIVATE horizon-hotfix-process-windows)
    add_dependencies(horizon-test-hotfix-windows-process-runner horizon-test-hotfix-process-child)
    add_dependencies(horizon-tests horizon-test-hotfix-windows-process-runner)

    if(MSVC)
        target_compile_options(horizon-test-hotfix-windows-process-runner PRIVATE /utf-8)
    endif()
    if(MINGW)
        target_link_options(horizon-test-hotfix-process-child PRIVATE -municode)
        target_link_options(horizon-test-hotfix-windows-process-runner PRIVATE -municode)
    endif()

    set(working_directory "${CMAKE_CURRENT_BINARY_DIR}/runner working directory")
    file(MAKE_DIRECTORY "${working_directory}")
    add_test(NAME horizon.hotfix.windows_process_runner
        COMMAND horizon-test-hotfix-windows-process-runner
            "$<TARGET_FILE:horizon-test-hotfix-process-child>" "${working_directory}")
    set_tests_properties(horizon.hotfix.windows_process_runner PROPERTIES
        ENVIRONMENT "HORIZON_PROCESS_TEST_INHERITED=parent;Horizon_Process_Test_Override=original"
        LABELS "hotfix;windows"
        TIMEOUT 30)
endif()

if(WIN32 AND CMAKE_GENERATOR MATCHES "^Ninja")
    add_executable(horizon-test-hotfix-cmake-build-backend test_cmake_build_backend.cpp)
    target_link_libraries(horizon-test-hotfix-cmake-build-backend PRIVATE
        horizon-hotfix-build-cmake horizon-hotfix-process-windows)
    if(MINGW)
        target_link_options(horizon-test-hotfix-cmake-build-backend PRIVATE -municode)
    endif()
    add_dependencies(horizon-tests horizon-test-hotfix-cmake-build-backend)

    set(cmake_backend_test_command
        "$<TARGET_FILE:horizon-test-hotfix-cmake-build-backend>"
        "${CMAKE_COMMAND}" "${CMAKE_MAKE_PROGRAM}" "${CMAKE_CXX_COMPILER}"
        "${HORIZON_BUILD_TOOLS_DIR}/cmake/horizon_hotfix_manifest.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_build_fixture/CMakeLists.txt"
        "${CMAKE_CURRENT_BINARY_DIR}")
    set(build_environment "${CMAKE_BINARY_DIR}/CMakeFiles/horizon-build-environment.json")
    if(HORIZON_DEV_PYTHON AND EXISTS "${build_environment}")
        add_test(NAME horizon.hotfix.cmake_build_backend
            COMMAND "${HORIZON_DEV_PYTHON}" "${HORIZON_BUILD_TOOLS_DIR}/scripts/run_build_tool.py"
                "${build_environment}" "" ${cmake_backend_test_command})
    else()
        add_test(NAME horizon.hotfix.cmake_build_backend COMMAND ${cmake_backend_test_command})
    endif()
    set_tests_properties(horizon.hotfix.cmake_build_backend PROPERTIES
        LABELS "hotfix;windows;integration"
        TIMEOUT 120)
endif()
