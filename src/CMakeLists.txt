file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapper/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapper/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapper/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapper/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/HardwareWrapperVulkan/*.h"
)

set(PUBLIC_HEADERS "${PROJECT_SOURCE_DIR}/include/Horizon.h")

add_subdirectory(Helicon)

add_library(Horizon STATIC ${SOURCE_FILES} ${PUBLIC_HEADERS})

target_include_directories(Horizon
    PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# 链接库依赖
target_link_libraries(volk PUBLIC Vulkan-Headers)

target_link_libraries(Horizon PUBLIC
    Helicon
    corona::kernel
    volk
    Vulkan-Headers
    VulkanMemoryAllocator
)

# 忽略第三方库（VulkanMemoryAllocator）的 nullability 警告
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(Horizon PRIVATE
        -Wno-nullability-completeness
    )
endif()