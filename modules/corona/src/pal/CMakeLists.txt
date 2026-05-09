set(CORONA_PAL_PUBLIC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/corona/pal")

set(CORONA_PAL_SOURCES
    common/file_system.cpp
    common/factory.cpp
)

set(CORONA_PAL_HEADERS
    ${CORONA_PAL_PUBLIC_INCLUDE_DIR}/cfw_platform.h
    ${CORONA_PAL_PUBLIC_INCLUDE_DIR}/i_dynamic_library.h
    ${CORONA_PAL_PUBLIC_INCLUDE_DIR}/i_file_system.h
)

add_library(corona_pal STATIC
    ${CORONA_PAL_SOURCES}
    ${CORONA_PAL_HEADERS}
)

add_library(corona::pal ALIAS corona_pal)

# Add platform-specific sources
if(WIN32)
    target_sources(corona_pal PRIVATE
        platform/windows/win_dynamic_library.cpp
    )
elseif(UNIX AND NOT APPLE)
    # Linux support - to be implemented
    # target_sources(corona_pal PRIVATE
    # platform/linux/linux_dynamic_library.cpp
    # )
    message(WARNING "Linux platform support not yet implemented")
elseif(APPLE)
    # macOS support - to be implemented
    # target_sources(corona_pal PRIVATE
    # platform/macos/macos_dynamic_library.cpp
    # )
    message(WARNING "macOS platform support not yet implemented")
endif()

if(CORONA_PAL_HEADERS)
    target_sources(corona_pal PRIVATE ${CORONA_PAL_HEADERS})
endif()

target_include_directories(corona_pal PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(corona_pal PUBLIC cxx_std_20)

# 安装规则
install(TARGETS corona_pal
    EXPORT CoronaFrameworkTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
