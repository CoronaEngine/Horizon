include_guard(GLOBAL)

set(HORIZON_SLANG_VERSION "2026.10" CACHE STRING "Slang binary distribution version")

find_package(slang ${HORIZON_SLANG_VERSION} CONFIG REQUIRED)

function(_horizon_slang_alias_target alias_name)
    if(TARGET ${alias_name})
        return()
    endif()

    foreach(candidate IN LISTS ARGN)
        if(TARGET ${candidate})
            add_library(${alias_name} INTERFACE IMPORTED GLOBAL)
            target_link_libraries(${alias_name} INTERFACE ${candidate})
            return()
        endif()
    endforeach()
endfunction()

_horizon_slang_alias_target(horizon::slang slang::slang)
_horizon_slang_alias_target(horizon::slang_rt slang::slang-rt)
_horizon_slang_alias_target(horizon::gfx slang::gfx)

foreach(_horizon_slang_required_target IN ITEMS horizon::slang horizon::slang_rt horizon::gfx)
    if(NOT TARGET ${_horizon_slang_required_target})
        message(FATAL_ERROR "Slang Conan package did not provide required target ${_horizon_slang_required_target}")
    endif()
endforeach()

set(HORIZON_SLANG_BINARY_DIRS)

foreach(_horizon_slang_package_folder_var IN ITEMS
    slang_PACKAGE_FOLDER_DEBUG
    slang_PACKAGE_FOLDER_RELEASE
    slang_PACKAGE_FOLDER_RELWITHDEBINFO
    slang_PACKAGE_FOLDER_MINSIZEREL
    slang_PACKAGE_FOLDER)
    if(DEFINED ${_horizon_slang_package_folder_var})
        list(APPEND HORIZON_SLANG_BINARY_DIRS "${${_horizon_slang_package_folder_var}}/bin")
    endif()
endforeach()

foreach(_horizon_slang_bin_dirs_var IN ITEMS slang_BIN_DIRS slang_BINDIRS)
    if(DEFINED ${_horizon_slang_bin_dirs_var})
        list(APPEND HORIZON_SLANG_BINARY_DIRS ${${_horizon_slang_bin_dirs_var}})
    endif()
endforeach()

if(HORIZON_SLANG_BINARY_DIRS)
    list(REMOVE_DUPLICATES HORIZON_SLANG_BINARY_DIRS)
endif()

if(WIN32 AND NOT HORIZON_SLANG_BINARY_DIRS)
    message(FATAL_ERROR "Slang Conan package did not expose a runtime bin directory")
endif()

if(DEFINED slang_VERSION_STRING)
    message(STATUS "Slang: using Conan package ${slang_VERSION_STRING}")
else()
    message(STATUS "Slang: using Conan package")
endif()
