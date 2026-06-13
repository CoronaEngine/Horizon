set(CORONA_KERNEL_PUBLIC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/corona/kernel")

set(CORONA_KERNEL_SOURCES
    core/logger.cpp
)

set(CORONA_KERNEL_HEADERS
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/core/callback_sink.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/core/i_logger.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/utils/stack_trace.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/utils/storage.h
    ${PROJECT_SOURCE_DIR}/include/corona/pal/cfw_platform.h
)

add_library(corona_kernel STATIC
    ${CORONA_KERNEL_SOURCES}
    ${CORONA_KERNEL_HEADERS}
)

add_library(corona::kernel ALIAS corona_kernel)

target_include_directories(corona_kernel PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(corona_kernel PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(corona_kernel PUBLIC
        /source-charset:utf-8
        /execution-charset:utf-8
    )
endif()

target_link_libraries(corona_kernel PUBLIC
    quill::quill
)

install(TARGETS corona_kernel
    EXPORT CoronaFrameworkTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
