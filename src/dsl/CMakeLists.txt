file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)
list(FILTER SOURCE_FILES EXCLUDE REGEX "/diagnostics/(env|printer)\\.cpp$")

add_library(horizon-dsl STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_include_directories(horizon-dsl
    PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(horizon-dsl PUBLIC horizon-core horizon-math horizon-ast)
target_compile_definitions(horizon-dsl PRIVATE OC_DSL_EXPORT_DLL=1)
