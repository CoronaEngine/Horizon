#pragma once
#include "quill/Logger.h"

#include <string>
#include <vector>

namespace Corona::Kernel {

// 前向声明
struct LogEntry;
class CallbackSink;

/**
 * @brief 日志级别枚举
 */
enum class LogLevel {
    trace,    ///< 跟踪级别，最详细的调试信息
    debug,    ///< 调试级别，用于开发调试
    info,     ///< 信息级别，常规运行信息
    warning,  ///< 警告级别，潜在问题
    error,    ///< 错误级别，错误但不致命
    fatal     ///< 致命级别，严重错误导致程序无法继续
};

/**
 * @brief Corona 日志系统 - Quill 的轻量封装
 *
 * CoronaLogger 直接使用 Quill 日志库，提供零开销的日志记录。
 *
 * 特性：
 * - 完全异步，无锁设计
 * - 自动捕获源代码位置信息（编译期宏展开）
 * - 线程安全的日志输出
 * - 极低延迟（~10-15ns）
 * - 支持格式化字符串（libfmt 语法）
 *
 * 使用示例：
 * @code
 * #include "corona/kernel/core/i_logger.h"
 *
 * CFW_LOG_INFO("程序启动");
 * CFW_LOG_WARNING("配置文件未找到，使用默认值: {}", default_value);
 * CFW_LOG_ERROR("网络连接失败，错误码: {}", error_code);
 * @endcode
 */
class CoronaLogger {
   public:
    /**
     * @brief 初始化日志系统
     *
     * 通常由 KernelContext 自动调用，也可手动调用确保初始化
     * 多次调用是安全的（使用 std::call_once）
     *
     * 自动创建：
     * - 控制台 Sink（输出到 stdout）
     * - 文件 Sink（格式: YYYY-MM-DD_HH-MM-SS_corona.log）
     */
    static void initialize();

    /**
     * @brief 设置日志级别
     * @param level 最低日志级别
     */
    static void set_log_level(LogLevel level);

    /**
     * @brief 刷新所有待处理的日志
     *
     * 强制将缓冲区的日志立即写入（通常用于程序退出前）
     */
    static void flush();

    /**
     * @brief 获取底层 Quill Logger（高级用户）
     * @return Quill logger 指针，用于直接调用 Quill API
     */
    static quill::Logger* get_logger();

    // ========== 回调 Sink 管理 ==========

    /**
     * @brief 获取全局 CallbackSink 实例
     *
     * 首次调用时自动创建并注入到 Logger 的 sinks 列表中。
     * @return CallbackSink 原始指针（生命周期由日志系统管理）
     */
    static CallbackSink* get_callback_sink();

    /**
     * @brief 便捷方法：拉取所有待处理的日志条目
     *
     * 等价于 get_callback_sink()->drain()。
     * 用于 Python 侧在 update 循环中主动拉取日志。
     * @return 自上次 drain 以来的所有 LogEntry
     */
    static std::vector<LogEntry> drain_logs();

    /**
     * @brief 设置 CallbackSink 的独立日志级别过滤
     *
     * 例如只让 INFO 及以上的日志进入前端队列，避免 TRACE/DEBUG 的大量推送。
     * @param min_level 最低日志级别
     */
    static void set_callback_sink_level(LogLevel min_level);

   private:
    CoronaLogger() = delete;
};

}  // namespace Corona::Kernel

// ========================================
// 日志宏 - 推荐使用方式
// ========================================

// 包含 Quill 宏定义
#include "quill/LogMacros.h"

#define CFW_LOG_FLUSH() ::Corona::Kernel::CoronaLogger::flush()

/**
 * @brief 跟踪级别日志（最详细）
 * 示例: CFW_LOG_TRACE("Processing item {}/{}", current, total);
 */
#define CFW_LOG_TRACE(fmt, ...) LOG_TRACE_L3(::Corona::Kernel::CoronaLogger::get_logger(), fmt, ##__VA_ARGS__)

/**
 * @brief 调试级别日志
 * 示例: CFW_LOG_DEBUG("Variable value: {}", value);
 */
#define CFW_LOG_DEBUG(fmt, ...) LOG_DEBUG(::Corona::Kernel::CoronaLogger::get_logger(), fmt, ##__VA_ARGS__)

/**
 * @brief 信息级别日志
 * 示例: CFW_LOG_INFO("Application started successfully");
 */
#define CFW_LOG_INFO(fmt, ...) LOG_INFO(::Corona::Kernel::CoronaLogger::get_logger(), fmt, ##__VA_ARGS__)

/**
 * @brief 通知级别日志
 * 示例: CFW_LOG_NOTICE("User {} logged in", username);
 */
#define CFW_LOG_NOTICE(fmt, ...) LOG_NOTICE(::Corona::Kernel::CoronaLogger::get_logger(), fmt, ##__VA_ARGS__)

/**
 * @brief 警告级别日志
 * 示例: CFW_LOG_WARNING("Configuration file not found");
 */
#define CFW_LOG_WARNING(fmt, ...) LOG_WARNING(::Corona::Kernel::CoronaLogger::get_logger(), fmt, ##__VA_ARGS__)

/**
 * @brief 错误级别日志
 * 示例: CFW_LOG_ERROR("Failed to connect: {}", error_message);
 */
#define CFW_LOG_ERROR(fmt, ...) LOG_ERROR(::Corona::Kernel::CoronaLogger::get_logger(), fmt, ##__VA_ARGS__)

/**
 * @brief 致命错误级别日志
 * 示例: CFW_LOG_CRITICAL("Critical system failure: {}", reason);
 */
#define CFW_LOG_CRITICAL(fmt, ...) LOG_CRITICAL(::Corona::Kernel::CoronaLogger::get_logger(), fmt, ##__VA_ARGS__)

// ========================================
// Python 模块日志宏 - 自动添加 [Python] 前缀
// ========================================

#define PY_LOG_TRACE(fmt, ...) LOG_TRACE_L3(::Corona::Kernel::CoronaLogger::get_logger(), "[Python] " fmt, ##__VA_ARGS__)
#define PY_LOG_DEBUG(fmt, ...) LOG_DEBUG(::Corona::Kernel::CoronaLogger::get_logger(), "[Python] " fmt, ##__VA_ARGS__)
#define PY_LOG_INFO(fmt, ...) LOG_INFO(::Corona::Kernel::CoronaLogger::get_logger(), "[Python] " fmt, ##__VA_ARGS__)
#define PY_LOG_NOTICE(fmt, ...) LOG_NOTICE(::Corona::Kernel::CoronaLogger::get_logger(), "[Python] " fmt, ##__VA_ARGS__)
#define PY_LOG_WARNING(fmt, ...) LOG_WARNING(::Corona::Kernel::CoronaLogger::get_logger(), "[Python] " fmt, ##__VA_ARGS__)
#define PY_LOG_ERROR(fmt, ...) LOG_ERROR(::Corona::Kernel::CoronaLogger::get_logger(), "[Python] " fmt, ##__VA_ARGS__)
#define PY_LOG_CRITICAL(fmt, ...) LOG_CRITICAL(::Corona::Kernel::CoronaLogger::get_logger(), "[Python] " fmt, ##__VA_ARGS__)

// ========================================
// Vue 模块日志宏 - 自动添加 [Vue] 前缀
// ========================================

#define VUE_LOG_TRACE(fmt, ...) LOG_TRACE_L3(::Corona::Kernel::CoronaLogger::get_logger(), "[Vue] " fmt, ##__VA_ARGS__)
#define VUE_LOG_DEBUG(fmt, ...) LOG_DEBUG(::Corona::Kernel::CoronaLogger::get_logger(), "[Vue] " fmt, ##__VA_ARGS__)
#define VUE_LOG_INFO(fmt, ...) LOG_INFO(::Corona::Kernel::CoronaLogger::get_logger(), "[Vue] " fmt, ##__VA_ARGS__)
#define VUE_LOG_NOTICE(fmt, ...) LOG_NOTICE(::Corona::Kernel::CoronaLogger::get_logger(), "[Vue] " fmt, ##__VA_ARGS__)
#define VUE_LOG_WARNING(fmt, ...) LOG_WARNING(::Corona::Kernel::CoronaLogger::get_logger(), "[Vue] " fmt, ##__VA_ARGS__)
#define VUE_LOG_ERROR(fmt, ...) LOG_ERROR(::Corona::Kernel::CoronaLogger::get_logger(), "[Vue] " fmt, ##__VA_ARGS__)
#define VUE_LOG_CRITICAL(fmt, ...) LOG_CRITICAL(::Corona::Kernel::CoronaLogger::get_logger(), "[Vue] " fmt, ##__VA_ARGS__)
