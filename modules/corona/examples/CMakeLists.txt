function(corona_add_example TARGET_NAME)
	add_executable(${TARGET_NAME} ${ARGN})
	target_link_libraries(${TARGET_NAME} PRIVATE corona::kernel corona::pal)
	corona_copy_tbb_runtime_artifacts(${TARGET_NAME})
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Examples")
endfunction()

# ========================================
# 01 - EventBus Example
# ========================================
corona_add_example(01_event_bus 01_event_bus/main.cpp)

# ========================================
# 02 - System Lifecycle Example
# ========================================
corona_add_example(02_system_lifecycle 02_system_lifecycle/main.cpp)

# ========================================
# 03 - EventStream Example
# ========================================
corona_add_example(03_event_stream 03_event_stream/main.cpp)

# ========================================
# 04 - VFS Example
# ========================================
corona_add_example(04_vfs 04_vfs/main.cpp)

# ========================================
# 05 - Multithreading Example
# ========================================
corona_add_example(05_multithreading 05_multithreading/main.cpp)

# ========================================
# 06 - Game Loop Example
# ========================================
corona_add_example(06_game_loop 06_game_loop/main.cpp)

# ========================================
# 07 - Storage Example
# ========================================
corona_add_example(07_storage 07_storage/main.cpp)

# ========================================
# 08 - ECS Example
# ========================================
corona_add_example(08_ecs 08_ecs/main.cpp)

# ========================================
# 09 - Memory Pool Example
# ========================================
corona_add_example(09_memory_pool 09_memory_pool/main.cpp)

# ========================================
# 10 - Coroutine Example
# ========================================
corona_add_example(10_coroutine 10_coroutine/main.cpp)
