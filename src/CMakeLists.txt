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
        "${PROJECT_SOURCE_DIR}/include/Horizon.h"
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
        # volk 在 macOS 上通过 dlopen("libvulkan.1.dylib") 查找 loader，而由 sshd、
        # Finder 或 launchd 启动的进程拿不到 DYLD_LIBRARY_PATH，因此把 Conan 提供的
        # loader 直接链接进来，由 dyld 在进程启动时加载。
        if(TARGET Vulkan::Loader)
            target_link_libraries(Horizon PRIVATE Vulkan::Loader)
        endif()
    endif()
endif()
