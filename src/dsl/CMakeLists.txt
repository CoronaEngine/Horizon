file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.c*)

add_library(horizon-dsl SHARED ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(horizon-dsl PUBLIC
    horizon-ast
    horizon-math
)
target_compile_definitions(horizon-dsl PRIVATE HORIZON_DSL_EXPORT_DLL=1)
set_target_properties(horizon-dsl PROPERTIES
    PUBLIC_HEADER "${HEADER_FILES}"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    FOLDER "horizon"
)
