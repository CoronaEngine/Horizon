file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)

add_library(horizon-math STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_include_directories(horizon-math
    PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

foreach(_core_dependency_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(_core_dependencies horizon-core ${_core_dependency_property})
    if(_core_dependencies MATCHES "(^|;)horizon-math($|;)")
        message(FATAL_ERROR "horizon-core must not depend on horizon-math")
    endif()
endforeach()

target_link_libraries(horizon-math PUBLIC horizon-core)
target_compile_definitions(horizon-math PRIVATE OC_MATH_EXPORT_DLL=1)
