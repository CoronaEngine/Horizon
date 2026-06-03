include_guard(GLOBAL)

set(HORIZON_FETCHCONTENT_SOURCE_ROOT
    "${PROJECT_SOURCE_DIR}/build/_deps"
    CACHE PATH "Shared FetchContent source checkouts used by all Horizon configure presets"
)
set(HORIZON_FETCHCONTENT_BINARY_ROOT
    "${PROJECT_BINARY_DIR}/deps"
    CACHE PATH "Preset-local FetchContent binary directories"
)
mark_as_advanced(HORIZON_FETCHCONTENT_SOURCE_ROOT HORIZON_FETCHCONTENT_BINARY_ROOT)

cmake_path(
    ABSOLUTE_PATH HORIZON_FETCHCONTENT_SOURCE_ROOT
    BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
    NORMALIZE
    OUTPUT_VARIABLE HORIZON_FETCHCONTENT_SOURCE_ROOT_ABSOLUTE
)
cmake_path(
    ABSOLUTE_PATH HORIZON_FETCHCONTENT_BINARY_ROOT
    BASE_DIRECTORY "${PROJECT_BINARY_DIR}"
    NORMALIZE
    OUTPUT_VARIABLE HORIZON_FETCHCONTENT_BINARY_ROOT_ABSOLUTE
)

set(FETCHCONTENT_BASE_DIR
    "${HORIZON_FETCHCONTENT_SOURCE_ROOT_ABSOLUTE}"
    CACHE PATH "Shared FetchContent population root"
    FORCE
)

message(STATUS "Horizon FetchContent source cache: ${HORIZON_FETCHCONTENT_SOURCE_ROOT_ABSOLUTE}")
message(STATUS "Horizon FetchContent build root: ${HORIZON_FETCHCONTENT_BINARY_ROOT_ABSOLUTE}")

function(horizon_fetchcontent_declare name)
    string(TOLOWER "${name}" lowercase_name)

    set(has_source_dir OFF)
    set(has_binary_dir OFF)
    foreach(arg IN LISTS ARGN)
        if(arg STREQUAL "SOURCE_DIR")
            set(has_source_dir ON)
        elseif(arg STREQUAL "BINARY_DIR")
            set(has_binary_dir ON)
        endif()
    endforeach()

    set(directory_args)
    if(NOT has_source_dir)
        list(APPEND directory_args
            SOURCE_DIR "${HORIZON_FETCHCONTENT_SOURCE_ROOT_ABSOLUTE}/${lowercase_name}-src"
        )
    endif()
    if(NOT has_binary_dir)
        list(APPEND directory_args
            BINARY_DIR "${HORIZON_FETCHCONTENT_BINARY_ROOT_ABSOLUTE}/${lowercase_name}-build"
        )
    endif()

    FetchContent_Declare(${name}
        ${ARGN}
        ${directory_args}
    )
endfunction()
