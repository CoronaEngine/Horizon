cmake_minimum_required(VERSION 4.0)

# Configure and Build are separate IDE processes. Restore the environment
# captured by Conan instead of inheriting an unrelated MSVC installation.
include("${HORIZON_BUILD_ENVIRONMENT}")

set(_command "execute_process(COMMAND")
set(_has_command FALSE)
set(_after_separator FALSE)
math(EXPR _last "${CMAKE_ARGC} - 1")
foreach(_index RANGE 0 ${_last})
    if(_after_separator)
        # A CMake list cannot preserve arguments ending in a backslash.
        # Quote each argument separately, choosing a delimiter it cannot close.
        set(_equals "=")
        while("${CMAKE_ARGV${_index}}" MATCHES "]${_equals}]")
            string(APPEND _equals "=")
        endwhile()
        string(APPEND _command " [${_equals}[${CMAKE_ARGV${_index}}]${_equals}]")
        set(_has_command TRUE)
    elseif(CMAKE_ARGV${_index} STREQUAL "--")
        set(_after_separator TRUE)
    endif()
endforeach()
if(NOT _has_command)
    message(FATAL_ERROR "No build tool was passed to the Horizon launcher")
endif()

# CMake decodes the console code page (or the ANSI code page without a
# console). Ninja otherwise compares /showIncludes output as raw bytes.
string(APPEND _command
    " ENCODING AUTO OUTPUT_VARIABLE _output ERROR_VARIABLE _output RESULT_VARIABLE _result)")
cmake_language(EVAL CODE "${_command}")
if(HORIZON_MSVC_SHOWINCLUDES_PREFIX)
    set(_output "\n${_output}")
    string(REPLACE "\n${HORIZON_MSVC_SHOWINCLUDES_PREFIX}"
                   "\nNote: including file: " _output "${_output}")
    string(SUBSTRING "${_output}" 1 -1 _output)
endif()
if(NOT _output STREQUAL "")
    message("${_output}")
endif()
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "Build tool failed (${_result})")
endif()
