function(horizon_add_test target)
    add_executable(${target} ${ARGN})

    target_link_libraries(${target} PRIVATE Horizon)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/src"
    )

    add_test(NAME ${target} COMMAND ${target})
endfunction()
