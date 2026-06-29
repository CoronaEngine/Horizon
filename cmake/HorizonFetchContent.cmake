include_guard(GLOBAL)

set(_horizon_fetchcontent_source_root_doc
    "Shared FetchContent source checkouts used by all Horizon configure presets"
)
set(_horizon_fetchcontent_binary_root_doc
    "Preset-local FetchContent binary directories"
)

set(_horizon_legacy_source_root "${PROJECT_SOURCE_DIR}/build/_deps")
cmake_path(
    ABSOLUTE_PATH _horizon_legacy_source_root
    BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
    NORMALIZE
    OUTPUT_VARIABLE _horizon_legacy_source_root_absolute
)

set(_horizon_fetchcontent_base_is_legacy OFF)
if(NOT PROJECT_IS_TOP_LEVEL
   AND DEFINED FETCHCONTENT_BASE_DIR
   AND NOT FETCHCONTENT_BASE_DIR STREQUAL "")
    cmake_path(
        ABSOLUTE_PATH FETCHCONTENT_BASE_DIR
        BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE _horizon_fetchcontent_base_absolute
    )

    if(_horizon_fetchcontent_base_absolute STREQUAL _horizon_legacy_source_root_absolute)
        set(_horizon_fetchcontent_base_is_legacy ON)
    endif()
endif()

if(PROJECT_IS_TOP_LEVEL)
    set(_horizon_default_source_root "${_horizon_legacy_source_root_absolute}")
elseif(DEFINED FETCHCONTENT_BASE_DIR
       AND NOT FETCHCONTENT_BASE_DIR STREQUAL ""
       AND NOT _horizon_fetchcontent_base_is_legacy)
    set(_horizon_default_source_root "${_horizon_fetchcontent_base_absolute}")
else()
    set(_horizon_default_source_root "${CMAKE_BINARY_DIR}/_deps")
endif()

if(DEFINED HORIZON_FETCHCONTENT_SOURCE_ROOT
   AND NOT HORIZON_FETCHCONTENT_SOURCE_ROOT STREQUAL "")
    cmake_path(
        ABSOLUTE_PATH HORIZON_FETCHCONTENT_SOURCE_ROOT
        BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE _horizon_existing_source_root_absolute
    )
else()
    set(_horizon_existing_source_root_absolute "")
endif()

if(NOT DEFINED HORIZON_FETCHCONTENT_SOURCE_ROOT
   OR HORIZON_FETCHCONTENT_SOURCE_ROOT STREQUAL "")
    set(HORIZON_FETCHCONTENT_SOURCE_ROOT
        "${_horizon_default_source_root}"
        CACHE PATH "${_horizon_fetchcontent_source_root_doc}"
        FORCE
    )
elseif(NOT PROJECT_IS_TOP_LEVEL
       AND _horizon_existing_source_root_absolute STREQUAL _horizon_legacy_source_root_absolute)
    set(HORIZON_FETCHCONTENT_SOURCE_ROOT
        "${_horizon_default_source_root}"
        CACHE PATH "${_horizon_fetchcontent_source_root_doc}"
        FORCE
    )
else()
    set(HORIZON_FETCHCONTENT_SOURCE_ROOT
        "${HORIZON_FETCHCONTENT_SOURCE_ROOT}"
        CACHE PATH "${_horizon_fetchcontent_source_root_doc}"
    )
endif()

if(NOT DEFINED HORIZON_FETCHCONTENT_BINARY_ROOT
   OR HORIZON_FETCHCONTENT_BINARY_ROOT STREQUAL "")
    set(HORIZON_FETCHCONTENT_BINARY_ROOT
        "${PROJECT_BINARY_DIR}/deps"
        CACHE PATH "${_horizon_fetchcontent_binary_root_doc}"
        FORCE
    )
else()
    set(HORIZON_FETCHCONTENT_BINARY_ROOT
        "${HORIZON_FETCHCONTENT_BINARY_ROOT}"
        CACHE PATH "${_horizon_fetchcontent_binary_root_doc}"
    )
endif()
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

if(_horizon_fetchcontent_base_is_legacy)
    set(FETCHCONTENT_BASE_DIR
        "${HORIZON_FETCHCONTENT_SOURCE_ROOT_ABSOLUTE}"
        CACHE PATH "Shared FetchContent population root"
        FORCE
    )
elseif(PROJECT_IS_TOP_LEVEL
       OR NOT DEFINED FETCHCONTENT_BASE_DIR
       OR FETCHCONTENT_BASE_DIR STREQUAL "")
    set(FETCHCONTENT_BASE_DIR
        "${HORIZON_FETCHCONTENT_SOURCE_ROOT_ABSOLUTE}"
        CACHE PATH "Shared FetchContent population root"
    )
endif()

unset(_horizon_default_source_root)
unset(_horizon_existing_source_root_absolute)
unset(_horizon_fetchcontent_base_absolute)
unset(_horizon_fetchcontent_base_is_legacy)
unset(_horizon_fetchcontent_binary_root_doc)
unset(_horizon_fetchcontent_source_root_doc)
unset(_horizon_legacy_source_root)
unset(_horizon_legacy_source_root_absolute)

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
