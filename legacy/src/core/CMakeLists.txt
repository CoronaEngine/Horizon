file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)
list(FILTER SOURCE_FILES EXCLUDE REGEX [[/runtime/platform_(windows|posix)\.cpp$]])
list(FILTER SOURCE_FILES EXCLUDE REGEX [[/util/logging\.cpp$]])

# Quill's backend and registries must be shared by the engine and Ocarina DLLs.
if(HORIZON_BUILD_OCARINA)
    add_library(horizon-logging SHARED util/logging.cpp)
    target_compile_definitions(horizon-logging
        PRIVATE QUILL_DLL_EXPORT
        INTERFACE QUILL_DLL_IMPORT)
    set_target_properties(horizon-logging PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
else()
    add_library(horizon-logging STATIC util/logging.cpp)
endif()
target_include_directories(horizon-logging
    PRIVATE "${PROJECT_SOURCE_DIR}/legacy/src"
    PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(horizon-logging PRIVATE OC_STATIC_LINK=1)
target_link_libraries(horizon-logging PUBLIC quill::quill
    PRIVATE horizon::fmt horizon::xxhash)

if(WIN32)
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/platform_windows.cpp")
elseif(APPLE OR UNIX)
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/platform_posix.cpp")
else()
    message(FATAL_ERROR "horizon-core has no platform backend for ${CMAKE_SYSTEM_NAME}")
endif()

add_library(horizon-core STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_sources(horizon-core PRIVATE
    "${PROJECT_SOURCE_DIR}/include/horizon/core/logging.h"
    "${PROJECT_SOURCE_DIR}/include/horizon/core/storage.h"
    "${PROJECT_SOURCE_DIR}/include/horizon/core/stack_trace.h"
)
target_include_directories(horizon-core
    PUBLIC
    "${PROJECT_SOURCE_DIR}/legacy/src"
    "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(horizon-core PUBLIC
    horizon-logging
    $<$<TARGET_EXISTS:horizon::fmt>:horizon::fmt>
    $<$<TARGET_EXISTS:horizon::xxhash>:horizon::xxhash>
)
target_link_libraries(horizon-core PRIVATE
    ${CMAKE_DL_LIBS}
    $<$<PLATFORM_ID:Windows>:dbghelp>
)
target_compile_definitions(horizon-core
    PUBLIC OC_STATIC_LINK=1
    PRIVATE OC_CORE_EXPORT_DLL=1
)
