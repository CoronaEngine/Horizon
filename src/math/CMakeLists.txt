file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.c*)

add_library(horizon-math SHARED ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(horizon-math PUBLIC horizon-core)
target_compile_definitions(horizon-math PRIVATE HORIZON_MATH_EXPORT_DLL=1)
set_target_properties(horizon-math PROPERTIES
    PUBLIC_HEADER "${HEADER_FILES}"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    FOLDER "horizon"
)
