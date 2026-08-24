file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)
list(FILTER SOURCE_FILES EXCLUDE REGEX [[/runtime/platform_(windows|posix)\.cpp$]])

if(WIN32)
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/platform_windows.cpp")
elseif(APPLE OR UNIX)
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/platform_posix.cpp")
else()
    message(FATAL_ERROR "horizon-core has no platform backend for ${CMAKE_SYSTEM_NAME}")
endif()

add_library(horizon-core STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_include_directories(horizon-core
    PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(horizon-core PUBLIC
    $<$<TARGET_EXISTS:horizon::fmt>:horizon::fmt>
    $<$<TARGET_EXISTS:horizon::xxhash>:horizon::xxhash>
)
target_link_libraries(horizon-core PRIVATE
    quill::quill
    ${CMAKE_DL_LIBS}
    $<$<PLATFORM_ID:Windows>:dbghelp>
)
target_compile_definitions(horizon-core
    PUBLIC OC_STATIC_LINK=1
    PRIVATE OC_CORE_EXPORT_DLL=1
)
