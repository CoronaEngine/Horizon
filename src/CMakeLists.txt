add_subdirectory(logging)
add_subdirectory(hotfix/api)

if(HORIZON_BUILD_ENGINE)
    file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapper/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapper/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/*.h"
    )

    if(APPLE)
        enable_language(OBJCXX)
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/DisplayVulkan/DisplayManager.cpp"
            PROPERTIES LANGUAGE OBJCXX
        )
    endif()

    set(PUBLIC_HEADERS
        "${PROJECT_SOURCE_DIR}/include/Horizon_old.h"
        "${PROJECT_SOURCE_DIR}/include/HardwareCommands_old.h"
        "${PROJECT_SOURCE_DIR}/include/corona/pal/cfw_platform.h"
    )

    add_subdirectory(Helicon)

    add_library(Horizon STATIC ${SOURCE_FILES} ${PUBLIC_HEADERS})
    if(MSVC)
        target_compile_options(Horizon PRIVATE /utf-8)
    endif()

    target_include_directories(Horizon
        PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_link_libraries(Horizon PUBLIC
        horizon-logging
        Helicon
        quill::quill
        horizon::volk
        horizon::vulkan_headers
        horizon::vma
    )

    # 忽略第三方库（VulkanMemoryAllocator）的 nullability 警告
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(Horizon PRIVATE
            -Wno-nullability-completeness
        )
    endif()

    # MacOS 依赖 Metal 系统库
    if(APPLE)
        target_link_libraries(Horizon PRIVATE
            "-framework AppKit"
            "-framework QuartzCore"
            "-framework Metal"
        )
    endif()
endif()
