include_guard(GLOBAL)

set(HORIZON_SLANG_VERSION "2026.10" CACHE STRING "Slang binary distribution version")
set(HORIZON_SLANG_ROOT "" CACHE PATH "Path to an existing Slang binary distribution")
set(HORIZON_SLANG_DOWNLOAD_URL "" CACHE STRING "Override URL for the Slang binary distribution archive")
set(HORIZON_SLANG_DOWNLOAD_SHA256 "" CACHE STRING "Override SHA256 for the Slang binary distribution archive")
option(HORIZON_SLANG_REQUIRE_LOCAL
    "Require a pre-populated Slang SDK root or archive instead of downloading"
    OFF)

set(_horizon_slang_managed_root "${PROJECT_SOURCE_DIR}/third-party/slang/src")
get_filename_component(_horizon_slang_managed_root "${_horizon_slang_managed_root}" ABSOLUTE)

if(NOT HORIZON_SLANG_ROOT)
    set(HORIZON_SLANG_ROOT "${_horizon_slang_managed_root}" CACHE PATH "Path to an existing Slang binary distribution" FORCE)
endif()

get_filename_component(HORIZON_SLANG_ROOT "${HORIZON_SLANG_ROOT}" ABSOLUTE)

set(_horizon_slang_custom_root OFF)
if(NOT HORIZON_SLANG_ROOT STREQUAL "${_horizon_slang_managed_root}")
    set(_horizon_slang_custom_root ON)
endif()

if(NOT WIN32)
    if(_horizon_slang_custom_root)
        message(STATUS "Slang: using custom root ${HORIZON_SLANG_ROOT}")
    else()
        message(FATAL_ERROR "Managed Slang download is currently configured for Windows packages. Set HORIZON_SLANG_ROOT to a local Slang distribution for this platform.")
    endif()
endif()

set(_horizon_slang_arch_input "${CMAKE_SYSTEM_PROCESSOR}")
if(DEFINED CMAKE_VS_PLATFORM_NAME AND NOT CMAKE_VS_PLATFORM_NAME STREQUAL "")
    set(_horizon_slang_arch_input "${CMAKE_VS_PLATFORM_NAME}")
endif()

string(TOLOWER "${_horizon_slang_arch_input}" _horizon_slang_arch_input_lower)
if(_horizon_slang_arch_input_lower MATCHES "^(x64|amd64|x86_64)$")
    set(_horizon_slang_arch "x86_64")
elseif(_horizon_slang_arch_input_lower MATCHES "^(arm64|aarch64)$")
    set(_horizon_slang_arch "aarch64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_horizon_slang_arch "x86_64")
else()
    message(FATAL_ERROR "Slang: unsupported Windows architecture '${_horizon_slang_arch_input}'. Use a 64-bit generator or set HORIZON_SLANG_ROOT.")
endif()

set(_horizon_slang_archive "slang-${HORIZON_SLANG_VERSION}-windows-${_horizon_slang_arch}.zip")
set(_horizon_slang_default_url "https://github.com/shader-slang/slang/releases/download/v${HORIZON_SLANG_VERSION}/${_horizon_slang_archive}")

if(NOT HORIZON_SLANG_DOWNLOAD_URL)
    set(HORIZON_SLANG_DOWNLOAD_URL "${_horizon_slang_default_url}")
endif()

if(NOT HORIZON_SLANG_DOWNLOAD_SHA256 AND HORIZON_SLANG_VERSION STREQUAL "2026.10")
    if(_horizon_slang_arch STREQUAL "x86_64")
        set(HORIZON_SLANG_DOWNLOAD_SHA256 "4d681fd6c40a028939d4907d714fb633a16895bd7ae8b8ef288401b805c17aa4")
    elseif(_horizon_slang_arch STREQUAL "aarch64")
        set(HORIZON_SLANG_DOWNLOAD_SHA256 "54b88155e5d94ddf63ef5013a59a8a49f35a7b415048fc57078d9eecdf2ccc7d")
    endif()
endif()

function(_horizon_slang_read_version root out_var)
    set(_version "")
    set(_version_file "${root}/include/slang-tag-version.h")

    if(EXISTS "${_version_file}")
        file(STRINGS "${_version_file}" _version_line REGEX "^#define SLANG_TAG_VERSION ")
        if(_version_line MATCHES "\"([^\"]+)\"")
            set(_version "${CMAKE_MATCH_1}")
        endif()
    endif()

    set(${out_var} "${_version}" PARENT_SCOPE)
endfunction()

function(_horizon_slang_is_ready root out_var)
    set(_required_files
        "include/slang.h"
        "include/slang-com-helper.h"
        "include/slang-com-ptr.h"
        "include/slang-tag-version.h"
        "lib/gfx.lib"
        "lib/slang.lib"
        "lib/slang-rt.lib"
        "bin/gfx.dll"
        "bin/slang.dll"
        "bin/slang-compiler.dll"
        "bin/slang-rt.dll"
    )

    foreach(_file IN LISTS _required_files)
        if(NOT EXISTS "${root}/${_file}")
            set(${out_var} OFF PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} ON PARENT_SCOPE)
endfunction()

_horizon_slang_is_ready("${HORIZON_SLANG_ROOT}" _horizon_slang_ready)
_horizon_slang_read_version("${HORIZON_SLANG_ROOT}" _horizon_slang_found_version)

set(_horizon_slang_needs_extract OFF)
if(NOT _horizon_slang_ready)
    set(_horizon_slang_needs_extract ON)
elseif(NOT _horizon_slang_found_version STREQUAL HORIZON_SLANG_VERSION)
    if(_horizon_slang_custom_root)
        message(FATAL_ERROR "Slang: custom root ${HORIZON_SLANG_ROOT} is version ${_horizon_slang_found_version}, expected ${HORIZON_SLANG_VERSION}.")
    endif()
    set(_horizon_slang_needs_extract ON)
endif()

if(_horizon_slang_needs_extract)
    if(_horizon_slang_custom_root)
        message(FATAL_ERROR "Slang: custom root ${HORIZON_SLANG_ROOT} is missing required files.")
    endif()

    set(_horizon_slang_dir "${PROJECT_SOURCE_DIR}/third-party/slang")
    set(_horizon_slang_download_dir "${_horizon_slang_dir}/download")
    set(_horizon_slang_archive_path "${_horizon_slang_download_dir}/${_horizon_slang_archive}")
    set(_horizon_slang_extract_dir "${_horizon_slang_dir}/extract-${HORIZON_SLANG_VERSION}")

    file(MAKE_DIRECTORY "${_horizon_slang_download_dir}")

    if(EXISTS "${_horizon_slang_archive_path}" AND HORIZON_SLANG_DOWNLOAD_SHA256)
        file(SHA256 "${_horizon_slang_archive_path}" _horizon_slang_archive_sha256)
        if(NOT _horizon_slang_archive_sha256 STREQUAL HORIZON_SLANG_DOWNLOAD_SHA256)
            message(STATUS "Slang: removing archive with mismatched hash: ${_horizon_slang_archive_path}")
            file(REMOVE "${_horizon_slang_archive_path}")
        endif()
    endif()

    if(NOT EXISTS "${_horizon_slang_archive_path}")
        if(HORIZON_SLANG_REQUIRE_LOCAL)
            message(FATAL_ERROR
                "Slang: local SDK/archive is required, but the archive is missing:\n"
                "  ${_horizon_slang_archive_path}\n"
                "Set HORIZON_SLANG_ROOT to a local Slang distribution or pre-populate "
                "the archive before configuring.")
        endif()

        message(STATUS "Slang: downloading ${HORIZON_SLANG_DOWNLOAD_URL} -> ${_horizon_slang_archive_path}")

        if(HORIZON_SLANG_DOWNLOAD_SHA256)
            file(DOWNLOAD
                "${HORIZON_SLANG_DOWNLOAD_URL}"
                "${_horizon_slang_archive_path}"
                SHOW_PROGRESS
                EXPECTED_HASH "SHA256=${HORIZON_SLANG_DOWNLOAD_SHA256}"
                STATUS _horizon_slang_download_status
                LOG _horizon_slang_download_log
            )
        else()
            file(DOWNLOAD
                "${HORIZON_SLANG_DOWNLOAD_URL}"
                "${_horizon_slang_archive_path}"
                SHOW_PROGRESS
                STATUS _horizon_slang_download_status
                LOG _horizon_slang_download_log
            )
        endif()

        list(GET _horizon_slang_download_status 0 _horizon_slang_download_code)
        list(GET _horizon_slang_download_status 1 _horizon_slang_download_message)
        if(NOT _horizon_slang_download_code EQUAL 0)
            message(FATAL_ERROR "Slang download failed (${_horizon_slang_download_code}): ${_horizon_slang_download_message}\n${_horizon_slang_download_log}")
        endif()
    endif()

    message(STATUS "Slang: extracting ${_horizon_slang_archive_path} -> ${HORIZON_SLANG_ROOT}")

    file(REMOVE_RECURSE "${_horizon_slang_extract_dir}")
    file(MAKE_DIRECTORY "${_horizon_slang_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_horizon_slang_archive_path}" DESTINATION "${_horizon_slang_extract_dir}")

    set(_horizon_slang_detected_root "")
    if(EXISTS "${_horizon_slang_extract_dir}/include/slang.h")
        set(_horizon_slang_detected_root "${_horizon_slang_extract_dir}")
    else()
        file(GLOB _horizon_slang_extract_children LIST_DIRECTORIES true "${_horizon_slang_extract_dir}/*")
        foreach(_child IN LISTS _horizon_slang_extract_children)
            if(EXISTS "${_child}/include/slang.h")
                set(_horizon_slang_detected_root "${_child}")
                break()
            endif()
        endforeach()
    endif()

    if(NOT _horizon_slang_detected_root)
        message(FATAL_ERROR "Slang archive did not contain include/slang.h")
    endif()

    file(REMOVE_RECURSE "${HORIZON_SLANG_ROOT}")
    if(_horizon_slang_detected_root STREQUAL _horizon_slang_extract_dir)
        file(RENAME "${_horizon_slang_extract_dir}" "${HORIZON_SLANG_ROOT}")
    else()
        file(RENAME "${_horizon_slang_detected_root}" "${HORIZON_SLANG_ROOT}")
        file(REMOVE_RECURSE "${_horizon_slang_extract_dir}")
    endif()
endif()

_horizon_slang_is_ready("${HORIZON_SLANG_ROOT}" _horizon_slang_ready)
if(NOT _horizon_slang_ready)
    message(FATAL_ERROR "Slang: required files are missing under ${HORIZON_SLANG_ROOT}")
endif()

_horizon_slang_read_version("${HORIZON_SLANG_ROOT}" _horizon_slang_found_version)
message(STATUS "Slang: ready at ${HORIZON_SLANG_ROOT} (${_horizon_slang_found_version})")

set(HORIZON_SLANG_INCLUDE_DIR "${HORIZON_SLANG_ROOT}/include")
set(HORIZON_SLANG_LIBRARY_DIR "${HORIZON_SLANG_ROOT}/lib")
set(HORIZON_SLANG_BINARY_DIR "${HORIZON_SLANG_ROOT}/bin")
