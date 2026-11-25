set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # All Horizon DLLs and their callers must share a CRT for malloc/free.
    # Use the target property exclusively so raw flags cannot select another CRT.
    get_directory_property(_horizon_runtime_flags COMPILE_OPTIONS)
    list(APPEND _horizon_runtime_flags "$ENV{CL}" "$ENV{_CL_}")
    foreach(_language IN ITEMS C CXX)
        foreach(_configuration IN ITEMS "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
            list(APPEND _horizon_runtime_flags "${CMAKE_${_language}_FLAGS${_configuration}}")
        endforeach()
    endforeach()
    if("${_horizon_runtime_flags}" MATCHES "(^|[ \t;:,\"'])[-/]M[DT]d?($|[ \t;:,>\"'])")
        message(FATAL_ERROR
            "Horizon requires a shared CRT. Remove raw /MT, /MTd, /MD or /MDd flags; "
            "the project selects /MDd for Debug and /MD for other configurations.")
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_compile_definitions(
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    NOMINMAX

    $<$<CONFIG:Debug>:CABBAGE_ENGINE_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:CABBAGE_ENGINE_DEBUG>
    $<$<CONFIG:Release>:CABBAGE_ENGINE_RELEASE>
    $<$<CONFIG:MinSizeRel>:CABBAGE_ENGINE_RELEASE>
)
