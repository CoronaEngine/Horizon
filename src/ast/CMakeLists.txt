file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.c*)

add_library(horizon-ast SHARED ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(horizon-ast PUBLIC horizon-core)
target_compile_definitions(horizon-ast PRIVATE HORIZON_AST_EXPORT_DLL=1)
set_target_properties(horizon-ast PROPERTIES
    PUBLIC_HEADER "${HEADER_FILES}"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    FOLDER "horizon"
)
