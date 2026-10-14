add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(image)
add_subdirectory(ast)
add_subdirectory(dsl)

if(HORIZON_BUILD_ENGINE)
    if(HORIZON_USE_LEGACY_BACKEND)
        file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapper/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapper/*.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/*.h"
        )
    else()
    file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.h"

        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.h"
    )
    endif()

    if(APPLE)
        enable_language(OBJCXX)
        # display_manager.cpp 中创建 MacOS 的 VkSurface 时使用了 Objc API
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/display/display_manager.cpp"
            PROPERTIES LANGUAGE OBJCXX
        )
        if(HORIZON_USE_LEGACY_BACKEND)
            set_source_files_properties(
                "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/DisplayVulkan/DisplayManager.cpp"
                PROPERTIES LANGUAGE OBJCXX)
        endif()
    endif()

    set(PUBLIC_HEADERS
        "${PROJECT_SOURCE_DIR}/include/horizon.h"
        "${PROJECT_SOURCE_DIR}/include/corona/pal/cfw_platform.h"
    )

    add_subdirectory(Helicon)
    add_library(Horizon STATIC ${SOURCE_FILES} ${PUBLIC_HEADERS})
    if(HORIZON_USE_LEGACY_BACKEND)
        # Helicon's resource proxies must use the same API types as its consumers.
        target_compile_definitions(Helicon PUBLIC HORIZON_LEGACY_API=1)
        target_sources(Horizon PRIVATE
            "${PROJECT_SOURCE_DIR}/include/Horizon_old.h"
            "${PROJECT_SOURCE_DIR}/include/HardwareCommands_old.h")
    endif()
    if(MSVC AND HORIZON_USE_LEGACY_BACKEND)
        target_compile_options(Horizon PRIVATE /utf-8)
    endif()

    target_include_directories(Horizon
        PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_link_libraries(Horizon PUBLIC
        horizon-core
        Helicon
        quill::quill
        horizon::volk
        horizon::vulkan_headers
        horizon::vma
    )

    # Tracy 采集客户端。PRIVATE：profiling 是实现细节，不做 profiling 的消费方不该
    # 被迫链 Tracy、也不该继承 HORIZON_TRACY_ENABLED。
    if(HORIZON_ENABLE_TRACY)
        target_link_libraries(Horizon PRIVATE Tracy::TracyClient)
        target_compile_definitions(Horizon PRIVATE HORIZON_TRACY_ENABLED=1)
    endif()

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
