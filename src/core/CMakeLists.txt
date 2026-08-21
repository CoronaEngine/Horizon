file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)

add_library(horizon-core STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_include_directories(horizon-core
    PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(horizon-core PUBLIC
    quill::quill
    $<$<TARGET_EXISTS:horizon::fmt>:horizon::fmt>
    $<$<TARGET_EXISTS:horizon::spdlog>:horizon::spdlog>
    $<$<TARGET_EXISTS:horizon::xxhash>:horizon::xxhash>
    $<$<PLATFORM_ID:Windows>:dbghelp>
)
target_compile_definitions(horizon-core PRIVATE OC_CORE_EXPORT_DLL=1)
