#pragma once

/**
 * @file coro.h
 * @brief Corona Framework 协程模块统一头文件
 *
 * 包含协程模块的所有公共 API。
 *
 * 核心组件：
 * - Task<T>: 异步任务类型，支持对称转移和异常传播
 * - Generator<T>: 惰性生成器，支持 range-based for
 * - Awaitable 工具类: suspend_for, yield, wait_until 等
 * - 执行器: IExecutor 接口和 TbbExecutor 实现
 * - 调度器: Scheduler 和 Runner 工具类
 *
 * 使用示例：
 * @code
 * #include <corona/kernel/coro/coro.h>
 *
 * using namespace Corona::Kernel::Coro;
 *
 * // 异步任务
 * Task<int> async_compute() {
 *     co_await suspend_for(std::chrono::milliseconds{100});
 *     co_return 42;
 * }
 *
 * // 生成器
 * Generator<int> range(int n) {
 *     for (int i = 0; i < n; ++i) {
 *         co_yield i;
 *     }
 * }
 *
 * // 运行任务
 * int main() {
 *     int result = Runner::run(async_compute());
 *     for (int n : range(10)) {
 *         std::cout << n << " ";
 *     }
 *     return 0;
 * }
 * @endcode
 */

// 协程概念定义
#include "coro_concepts.h"

// 核心类型
#include "generator.h"
#include "task.h"

// Awaitable 工具
#include "awaitables.h"

// 执行器
#include "executor.h"
#include "tbb_executor.h"

// 调度器
#include "scheduler.h"

// 并行组合
#include "when_all.h"

// 同步原语
#include "async_scope.h"
#include "sync.h"

namespace Corona::Kernel::Coro {

/**
 * @brief 协程模块版本信息
 */
struct Version {
    static constexpr int major = 1;
    static constexpr int minor = 0;
    static constexpr int patch = 0;
    static constexpr const char* string = "1.0.0";
};

}  // namespace Corona::Kernel::Coro
