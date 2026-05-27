function(horizon_add_benchmark target)
    add_executable(${target} ${ARGN})

    target_link_libraries(${target} PRIVATE Horizon)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/benchmarks"
        "${PROJECT_SOURCE_DIR}/src"
    )

    set_target_properties(${target} PROPERTIES
        FOLDER "Benchmarks"
    )
endfunction()
