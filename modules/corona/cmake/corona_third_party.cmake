include_guard(GLOBAL)

include(FetchContent)

if(NOT HORIZON_DEPENDENCY_PROVIDER STREQUAL "fetchcontent")
    find_package(quill CONFIG QUIET)
endif()

if(TARGET quill::quill)
    return()
endif()

if(HORIZON_DEPENDENCY_PROVIDER STREQUAL "conan")
    find_package(quill CONFIG REQUIRED)
endif()

if(TARGET quill::quill)
    return()
endif()

if(COMMAND horizon_fetchcontent_declare)
    horizon_fetchcontent_declare(
        quill
        EXCLUDE_FROM_ALL TRUE
    )
else()
    if(NOT DEFINED FETCHCONTENT_BASE_DIR OR FETCHCONTENT_BASE_DIR STREQUAL "")
        set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "FetchContent source cache")
    endif()

    cmake_path(
        ABSOLUTE_PATH FETCHCONTENT_BASE_DIR
        BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE _corona_fetchcontent_base_absolute
    )
    set(_corona_quill_source_dir "${_corona_fetchcontent_base_absolute}/quill-src")
    if(NOT EXISTS "${_corona_quill_source_dir}")
        message(FATAL_ERROR
            "Corona quill source cache is required, but this directory does not exist: "
            "${_corona_quill_source_dir}")
    endif()

    FetchContent_Declare(
        quill
        SOURCE_DIR "${_corona_quill_source_dir}"
        EXCLUDE_FROM_ALL TRUE
    )
endif()

FetchContent_MakeAvailable(quill)
