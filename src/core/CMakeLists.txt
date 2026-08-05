file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.c*)

add_library(horizon-core SHARED ${HEADER_FILES} ${SOURCE_FILES})
target_include_directories(horizon-core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(horizon-core PUBLIC
    $<$<PLATFORM_ID:Windows>:dbghelp>
)
target_compile_definitions(horizon-core PRIVATE HORIZON_CORE_EXPORT_DLL=1)
set_target_properties(horizon-core PROPERTIES
    PUBLIC_HEADER "${HEADER_FILES}"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    FOLDER "horizon"
)
