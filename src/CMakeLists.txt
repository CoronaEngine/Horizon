file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.h"

    "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.h"

    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.h"
)

set(PUBLIC_HEADERS
    "${PROJECT_SOURCE_DIR}/include/horizon.h"
    "${PROJECT_SOURCE_DIR}/include/corona/kernel/core/i_logger.h"
    "${PROJECT_SOURCE_DIR}/include/corona/kernel/utils/stack_trace.h"
    "${PROJECT_SOURCE_DIR}/include/corona/kernel/utils/storage.h"
    "${PROJECT_SOURCE_DIR}/include/corona/pal/cfw_platform.h"
)

add_subdirectory(Helicon)

add_library(Horizon STATIC ${SOURCE_FILES} ${PUBLIC_HEADERS})

target_include_directories(Horizon
    PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)


target_link_libraries(Horizon PUBLIC
    Helicon
    quill::quill
    horizon::volk
    horizon::vulkan_headers
    horizon::vma
)

# Tracy 采集客户端。PRIVATE：profiling 是实现细节，不做 profiling 的消费方不该
# 被迫链 Tracy、也不该继承 HORIZON_TRACY_ENABLED。
#
# Horizon 是 STATIC 库，CMake 会把 PRIVATE 依赖以 $<LINK_ONLY:> 记进
# INTERFACE_LINK_LIBRARIES，所以最终链接照样解析到 Tracy 符号；下游只是不再继承
# include 目录和 define。
#
# 想埋点的 in-tree 目标须自行 opt-in（见 examples/CMakeLists.txt）——注意宏在缺少
# define 时展开为空且不报错，埋点会静默消失。
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
