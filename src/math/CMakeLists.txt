file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)

add_library(horizon-math STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_include_directories(horizon-math
    PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(horizon-math PUBLIC horizon-core)
target_compile_definitions(horizon-math PRIVATE OC_MATH_EXPORT_DLL=1)
