include_guard(GLOBAL)

set(HORIZON_SLANG_VERSION "2026.10" CACHE STRING "Slang binary distribution version")

find_package(slang ${HORIZON_SLANG_VERSION} CONFIG REQUIRED)

_horizon_alias_target(horizon::slang slang::slang)
_horizon_alias_target(horizon::slang_rt slang::slang-rt)
_horizon_alias_target(horizon::gfx slang::gfx)

# Collect the pinned Conan package's DLLs once for Helicon and Ocarina.
set(HORIZON_SLANG_RUNTIME_FILES)
if(WIN32)
    foreach(_config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        foreach(_bin_dir IN LISTS slang_BIN_DIRS_${_config})
            file(GLOB _dlls CONFIGURE_DEPENDS "${_bin_dir}/*.dll")
            list(APPEND HORIZON_SLANG_RUNTIME_FILES ${_dlls})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES HORIZON_SLANG_RUNTIME_FILES)
    if(NOT HORIZON_SLANG_RUNTIME_FILES)
        message(FATAL_ERROR "Slang runtime DLLs were not found in the Conan package")
    endif()
endif()
set_property(GLOBAL PROPERTY HORIZON_SLANG_RUNTIME_FILES "${HORIZON_SLANG_RUNTIME_FILES}")
