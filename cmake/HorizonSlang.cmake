include_guard(GLOBAL)

set(HORIZON_SLANG_VERSION "2026.10" CACHE STRING "Slang binary distribution version")
set(HORIZON_SLANG_ROOT "" CACHE PATH "Path to an existing Slang binary distribution")
set(HORIZON_SLANG_DOWNLOAD_URL "" CACHE STRING "Override URL for the Slang binary distribution archive")
set(HORIZON_SLANG_DOWNLOAD_SHA256 "" CACHE STRING "Override SHA256 for the Slang binary distribution archive")

set(_HORIZON_SLANG_MANAGED_ROOT "${PROJECT_SOURCE_DIR}/third-party/slang/src")
get_filename_component(_HORIZON_SLANG_MANAGED_ROOT "${_HORIZON_SLANG_MANAGED_ROOT}" ABSOLUTE)

if(NOT HORIZON_SLANG_ROOT)
    set(HORIZON_SLANG_ROOT "${_HORIZON_SLANG_MANAGED_ROOT}" CACHE PATH "Path to an existing Slang binary distribution" FORCE)
endif()

get_filename_component(HORIZON_SLANG_ROOT "${HORIZON_SLANG_ROOT}" ABSOLUTE)

set(_HORIZON_SLANG_CUSTOM_ROOT OFF)
if(NOT HORIZON_SLANG_ROOT STREQUAL _HORIZON_SLANG_MANAGED_ROOT)
    set(_HORIZON_SLANG_CUSTOM_ROOT ON)
endif()

if(NOT WIN32)
    if(_HORIZON_SLANG_CUSTOM_ROOT)
        message(STATUS "Slang: using custom root ${HORIZON_SLANG_ROOT}")
    else()
        message(FATAL_ERROR "Managed Slang download is currently configured for Windows packages. Set HORIZON_SLANG_ROOT to a local Slang distribution for this platform.")
    endif()
endif()

set(_HORIZON_SLANG_ARCH_INPUT "${CMAKE_SYSTEM_PROCESSOR}")
if(DEFINED CMAKE_VS_PLATFORM_NAME AND NOT CMAKE_VS_PLATFORM_NAME STREQUAL "")
    set(_HORIZON_SLANG_ARCH_INPUT "${CMAKE_VS_PLATFORM_NAME}")
endif()

string(TOLOWER "${_HORIZON_SLANG_ARCH_INPUT}" _HORIZON_SLANG_ARCH_INPUT_LOWER)
if(_HORIZON_SLANG_ARCH_INPUT_LOWER MATCHES "^(x64|amd64|x86_64)$")
    set(_HORIZON_SLANG_ARCH "x86_64")
elseif(_HORIZON_SLANG_ARCH_INPUT_LOWER MATCHES "^(arm64|aarch64)$")
    set(_HORIZON_SLANG_ARCH "aarch64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_HORIZON_SLANG_ARCH "x86_64")
else()
    message(FATAL_ERROR "Slang: unsupported Windows architecture '${_HORIZON_SLANG_ARCH_INPUT}'. Use a 64-bit generator or set HORIZON_SLANG_ROOT.")
endif()

set(_HORIZON_SLANG_ARCHIVE "slang-${HORIZON_SLANG_VERSION}-windows-${_HORIZON_SLANG_ARCH}.zip")
set(_HORIZON_SLANG_DEFAULT_URL "https://github.com/shader-slang/slang/releases/download/v${HORIZON_SLANG_VERSION}/${_HORIZON_SLANG_ARCHIVE}")

if(NOT HORIZON_SLANG_DOWNLOAD_URL)
    set(HORIZON_SLANG_DOWNLOAD_URL "${_HORIZON_SLANG_DEFAULT_URL}")
endif()

if(NOT HORIZON_SLANG_DOWNLOAD_SHA256 AND HORIZON_SLANG_VERSION STREQUAL "2026.10")
    if(_HORIZON_SLANG_ARCH STREQUAL "x86_64")
        set(HORIZON_SLANG_DOWNLOAD_SHA256 "4d681fd6c40a028939d4907d714fb633a16895bd7ae8b8ef288401b805c17aa4")
    elseif(_HORIZON_SLANG_ARCH STREQUAL "aarch64")
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

_horizon_slang_is_ready("${HORIZON_SLANG_ROOT}" _HORIZON_SLANG_READY)
_horizon_slang_read_version("${HORIZON_SLANG_ROOT}" _HORIZON_SLANG_FOUND_VERSION)

set(_HORIZON_SLANG_NEEDS_EXTRACT OFF)
if(NOT _HORIZON_SLANG_READY)
    set(_HORIZON_SLANG_NEEDS_EXTRACT ON)
elseif(NOT _HORIZON_SLANG_FOUND_VERSION STREQUAL HORIZON_SLANG_VERSION)
    if(_HORIZON_SLANG_CUSTOM_ROOT)
        message(FATAL_ERROR "Slang: custom root ${HORIZON_SLANG_ROOT} is version ${_HORIZON_SLANG_FOUND_VERSION}, expected ${HORIZON_SLANG_VERSION}.")
    endif()
    set(_HORIZON_SLANG_NEEDS_EXTRACT ON)
endif()

if(_HORIZON_SLANG_NEEDS_EXTRACT)
    if(_HORIZON_SLANG_CUSTOM_ROOT)
        message(FATAL_ERROR "Slang: custom root ${HORIZON_SLANG_ROOT} is missing required files.")
    endif()

    set(_HORIZON_SLANG_DIR "${PROJECT_SOURCE_DIR}/third-party/slang")
    set(_HORIZON_SLANG_DOWNLOAD_DIR "${_HORIZON_SLANG_DIR}/download")
    set(_HORIZON_SLANG_ARCHIVE_PATH "${_HORIZON_SLANG_DOWNLOAD_DIR}/${_HORIZON_SLANG_ARCHIVE}")
    set(_HORIZON_SLANG_EXTRACT_DIR "${_HORIZON_SLANG_DIR}/extract-${HORIZON_SLANG_VERSION}")

    file(MAKE_DIRECTORY "${_HORIZON_SLANG_DOWNLOAD_DIR}")

    if(EXISTS "${_HORIZON_SLANG_ARCHIVE_PATH}" AND HORIZON_SLANG_DOWNLOAD_SHA256)
        file(SHA256 "${_HORIZON_SLANG_ARCHIVE_PATH}" _HORIZON_SLANG_ARCHIVE_SHA256)
        if(NOT _HORIZON_SLANG_ARCHIVE_SHA256 STREQUAL HORIZON_SLANG_DOWNLOAD_SHA256)
            message(STATUS "Slang: removing archive with mismatched hash: ${_HORIZON_SLANG_ARCHIVE_PATH}")
            file(REMOVE "${_HORIZON_SLANG_ARCHIVE_PATH}")
        endif()
    endif()

    if(NOT EXISTS "${_HORIZON_SLANG_ARCHIVE_PATH}")
        message(STATUS "Slang: downloading ${HORIZON_SLANG_DOWNLOAD_URL} -> ${_HORIZON_SLANG_ARCHIVE_PATH}")

        if(HORIZON_SLANG_DOWNLOAD_SHA256)
            file(DOWNLOAD
                "${HORIZON_SLANG_DOWNLOAD_URL}"
                "${_HORIZON_SLANG_ARCHIVE_PATH}"
                SHOW_PROGRESS
                EXPECTED_HASH "SHA256=${HORIZON_SLANG_DOWNLOAD_SHA256}"
                STATUS _HORIZON_SLANG_DOWNLOAD_STATUS
                LOG _HORIZON_SLANG_DOWNLOAD_LOG
            )
        else()
            file(DOWNLOAD
                "${HORIZON_SLANG_DOWNLOAD_URL}"
                "${_HORIZON_SLANG_ARCHIVE_PATH}"
                SHOW_PROGRESS
                STATUS _HORIZON_SLANG_DOWNLOAD_STATUS
                LOG _HORIZON_SLANG_DOWNLOAD_LOG
            )
        endif()

        list(GET _HORIZON_SLANG_DOWNLOAD_STATUS 0 _HORIZON_SLANG_DOWNLOAD_CODE)
        list(GET _HORIZON_SLANG_DOWNLOAD_STATUS 1 _HORIZON_SLANG_DOWNLOAD_MESSAGE)
        if(NOT _HORIZON_SLANG_DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "Slang download failed (${_HORIZON_SLANG_DOWNLOAD_CODE}): ${_HORIZON_SLANG_DOWNLOAD_MESSAGE}\n${_HORIZON_SLANG_DOWNLOAD_LOG}")
        endif()
    endif()

    message(STATUS "Slang: extracting ${_HORIZON_SLANG_ARCHIVE_PATH} -> ${HORIZON_SLANG_ROOT}")

    file(REMOVE_RECURSE "${_HORIZON_SLANG_EXTRACT_DIR}")
    file(MAKE_DIRECTORY "${_HORIZON_SLANG_EXTRACT_DIR}")
    file(ARCHIVE_EXTRACT INPUT "${_HORIZON_SLANG_ARCHIVE_PATH}" DESTINATION "${_HORIZON_SLANG_EXTRACT_DIR}")

    set(_HORIZON_SLANG_DETECTED_ROOT "")
    if(EXISTS "${_HORIZON_SLANG_EXTRACT_DIR}/include/slang.h")
        set(_HORIZON_SLANG_DETECTED_ROOT "${_HORIZON_SLANG_EXTRACT_DIR}")
    else()
        file(GLOB _HORIZON_SLANG_EXTRACT_CHILDREN LIST_DIRECTORIES true "${_HORIZON_SLANG_EXTRACT_DIR}/*")
        foreach(_child IN LISTS _HORIZON_SLANG_EXTRACT_CHILDREN)
            if(EXISTS "${_child}/include/slang.h")
                set(_HORIZON_SLANG_DETECTED_ROOT "${_child}")
                break()
            endif()
        endforeach()
    endif()

    if(NOT _HORIZON_SLANG_DETECTED_ROOT)
        message(FATAL_ERROR "Slang archive did not contain include/slang.h")
    endif()

    file(REMOVE_RECURSE "${HORIZON_SLANG_ROOT}")
    if(_HORIZON_SLANG_DETECTED_ROOT STREQUAL _HORIZON_SLANG_EXTRACT_DIR)
        file(RENAME "${_HORIZON_SLANG_EXTRACT_DIR}" "${HORIZON_SLANG_ROOT}")
    else()
        file(RENAME "${_HORIZON_SLANG_DETECTED_ROOT}" "${HORIZON_SLANG_ROOT}")
        file(REMOVE_RECURSE "${_HORIZON_SLANG_EXTRACT_DIR}")
    endif()
endif()

_horizon_slang_is_ready("${HORIZON_SLANG_ROOT}" _HORIZON_SLANG_READY)
if(NOT _HORIZON_SLANG_READY)
    message(FATAL_ERROR "Slang: required files are missing under ${HORIZON_SLANG_ROOT}")
endif()

_horizon_slang_read_version("${HORIZON_SLANG_ROOT}" _HORIZON_SLANG_FOUND_VERSION)
message(STATUS "Slang: ready at ${HORIZON_SLANG_ROOT} (${_HORIZON_SLANG_FOUND_VERSION})")

set(HORIZON_SLANG_INCLUDE_DIR "${HORIZON_SLANG_ROOT}/include")
set(HORIZON_SLANG_LIBRARY_DIR "${HORIZON_SLANG_ROOT}/lib")
set(HORIZON_SLANG_BINARY_DIR "${HORIZON_SLANG_ROOT}/bin")
