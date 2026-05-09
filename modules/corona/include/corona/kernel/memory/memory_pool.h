#pragma once

/// @file memory_pool.h
/// @brief Corona Framework 内存池模块统一头文件
///
/// 包含所有内存池相关的类和工具：
/// - PoolConfig: 内存池配置
/// - FixedPool: 固定大小块内存池
/// - ObjectPool: 类型安全对象池
/// - LinearArena: 线性分配器
/// - FrameArena: 双缓冲帧分配器
/// - LockFreeStack: 无锁空闲链表
/// - ThreadLocalPool: 线程本地对象池

#include "cache_aligned_allocator.h"
#include "chunk.h"
#include "fixed_pool.h"
#include "frame_arena.h"
#include "linear_arena.h"
#include "lock_free_stack.h"
#include "object_pool.h"
#include "pool_config.h"
#include "thread_local_pool.h"

namespace Corona::Kernal::Memory {

/// @brief 内存池模块版本
inline constexpr const char* kMemoryPoolVersion = "1.0.0";

}  // namespace Corona::Kernal::Memory
