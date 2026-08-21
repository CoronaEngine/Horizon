file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)

add_library(horizon-ast STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_include_directories(horizon-ast
    PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(horizon-ast PUBLIC horizon-core horizon-math)
target_compile_definitions(horizon-ast PRIVATE OC_AST_EXPORT_DLL=1)
