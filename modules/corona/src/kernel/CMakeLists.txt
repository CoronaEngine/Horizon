set(CORONA_KERNEL_PUBLIC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/corona/kernel")

set(CORONA_KERNEL_SOURCES
    core/logger.cpp
    event/event_bus.cpp
    event/event_stream.cpp
    core/vfs.cpp
    core/plugin_manager.cpp
    core/kernel_context.cpp
    system/system_manager.cpp
    system/system_base.cpp
    memory/cache_aligned_allocator.cpp
    memory/fixed_pool.cpp
    memory/linear_arena.cpp
    memory/frame_arena.cpp
    utils/work_stealing_queue.cpp
    utils/task_scheduler.cpp
    utils/task_group.cpp
    ecs/archetype_signature.cpp
    ecs/archetype_layout.cpp
    ecs/chunk.cpp
    ecs/chunk_allocator.cpp
    ecs/archetype.cpp
    ecs/entity_manager.cpp
    ecs/world.cpp
)

set(CORONA_KERNEL_HEADERS
    # coro
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/coro/coro.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/coro/coro_concepts.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/coro/task.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/coro/generator.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/coro/awaitables.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/coro/executor.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/coro/tbb_executor.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/coro/scheduler.h
    # core
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/core/i_logger.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/core/i_plugin_manager.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/core/i_vfs.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/core/kernel_context.h
    # ecs
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/archetype.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/archetype_layout.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/archetype_signature.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/chunk.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/chunk_allocator.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/component.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/ecs_types.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/entity_id.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/entity_record.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/entity_manager.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/ecs/world.h
    # event
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/event/event_concepts.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/event/i_event_bus.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/event/i_event_stream.h
    # memory
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/cache_aligned_allocator.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/pool_config.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/chunk.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/fixed_pool.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/object_pool.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/linear_arena.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/frame_arena.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/lock_free_stack.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/thread_local_pool.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/memory/memory_pool.h
    # system
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/system/i_system.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/system/i_system_context.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/system/i_system_manager.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/system/system_base.h
    # utils
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/utils/lock_free_queue.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/utils/stack_trace.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/utils/storage.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/utils/task_group.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/utils/task_scheduler.h
    ${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/utils/work_stealing_queue.h
)

add_library(corona_kernel STATIC
    ${CORONA_KERNEL_SOURCES}
    ${CORONA_KERNEL_HEADERS})

add_library(corona::kernel ALIAS corona_kernel)

if(CORONA_KERNEL_HEADERS)
    target_sources(corona_kernel PRIVATE ${CORONA_KERNEL_HEADERS})
endif()

target_include_directories(corona_kernel PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(corona_kernel PUBLIC cxx_std_20)
target_link_libraries(corona_kernel PUBLIC
    corona::pal
    TBB::tbb
    quill::quill
)

# 安装规则
install(TARGETS corona_kernel
    EXPORT CoronaFrameworkTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
