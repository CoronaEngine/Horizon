file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)

add_library(horizon-image STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_include_directories(horizon-image PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(horizon-image PUBLIC cxx_std_20)
target_link_libraries(horizon-image
    PUBLIC
        horizon-core
        horizon-math
    PRIVATE
        horizon::stb
        horizon::tinyexr
)
