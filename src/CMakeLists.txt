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

set(PUBLIC_HEADERS "${PROJECT_SOURCE_DIR}/include/horizon.h")

add_subdirectory(Helicon)

add_library(Horizon STATIC ${SOURCE_FILES} ${PUBLIC_HEADERS})

add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(ast)
add_subdirectory(dsl)

target_include_directories(Horizon
    PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(HORIZON_VALIDATION_DEFINE
    "$<IF:$<CONFIG:Debug>,$<BOOL:${HORIZON_ENABLE_DEBUG_VALIDATION}>,$<IF:$<CONFIG:RelWithDebInfo>,$<BOOL:${HORIZON_ENABLE_RELWITHDEBINFO_VALIDATION}>,$<BOOL:${HORIZON_ENABLE_RELEASE_VALIDATION}>>>"
)

target_compile_definitions(Horizon
    PUBLIC
    HORIZON_ENABLE_HARDWARE_VALIDATION=${HORIZON_VALIDATION_DEFINE}
    PRIVATE
    HORIZON_ENABLE_VULKAN_VALIDATION=${HORIZON_VALIDATION_DEFINE}
)

# 链接库依赖
target_link_libraries(volk PUBLIC Vulkan-Headers)

target_link_libraries(Horizon PUBLIC
    horizon-core
    horizon-math
    horizon-ast
    horizon-dsl
    Helicon
    corona::kernel
    volk
    Vulkan-Headers
    VulkanMemoryAllocator
)

# Tracy 采集客户端；PUBLIC 传递给 examples 等下游目标，
# 使它们也能直接使用 horizon_profiling.h 中的埋点宏。
if(HORIZON_ENABLE_TRACY)
    target_link_libraries(Horizon PUBLIC Tracy::TracyClient)
    target_compile_definitions(Horizon PUBLIC HORIZON_TRACY_ENABLED=1)
endif()

# 忽略第三方库（VulkanMemoryAllocator）的 nullability 警告
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(Horizon PRIVATE
        -Wno-nullability-completeness
    )
endif()
