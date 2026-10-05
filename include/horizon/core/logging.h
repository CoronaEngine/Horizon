#pragma once
#include <filesystem>
#include <quill/Logger.h>

// Public logging declarations must not import Core's generic core/... headers:
// applications may also include the legacy renderer's headers with those names.
namespace horizon::core {

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

struct LoggingOptions
{
    bool console = true;
    // Empty disables file output. A configured file is created/truncated once.
    std::filesystem::path file_path;
    bool install_signal_handlers = false;
    // Enables console colours and, on Windows, UTF-8 and ANSI console setup.
    bool configure_utf8_console = false;
#ifndef NDEBUG
    LogLevel level = LogLevel::Debug;
#else
    LogLevel level = LogLevel::Info;
#endif
};

// Call at application startup, before any logging or direct Quill startup.
// The first successful initialization wins; subsequent calls are no-ops.
// Without an explicit call, the first message uses the console-only defaults.
void initialize_logging(const LoggingOptions &options = {});
void set_log_level(LogLevel level) noexcept;
void log_flush() noexcept;
quill::Logger *get_quill_logger();

}  // namespace horizon::core

namespace Corona::Kernel
{

    /**
     * @brief 日志级别枚举
     */
    enum class LogLevel
    {
        trace,   ///< 跟踪级别，最详细的调试信息
        debug,   ///< 调试级别，用于开发调试
        info,    ///< 信息级别，常规运行信息
        warning, ///< 警告级别，潜在问题
        error,   ///< 错误级别，错误但不致命
        fatal    ///< 致命级别，严重错误导致程序无法继续
    };

    /**
     * @brief 旧 Corona 日志接口的兼容层，转发至 horizon::core 的统一日志实例。
     *
     * 文件输出和信号处理由应用在首次记录日志前通过
     * horizon::core::initialize_logging 配置；默认只输出到控制台。
     * 旧宏仍保留格式化、调用位置及 Python/Vue 前缀。
     *
     * 使用示例：
     * @code
     * #include "horizon/core/logging.h"
     *
     * CFW_LOG_INFO("程序启动");
     * CFW_LOG_WARNING("配置文件未找到，使用默认值: {}", default_value);
     * CFW_LOG_ERROR("网络连接失败，错误码: {}", error_code);
     * @endcode
     */
    class CoronaLogger
    {
    public:
        /**
         * @brief 初始化日志系统
         *
         * 复用 Core 已有配置；未初始化时使用 Core 默认配置。
         * 多次调用不会重置日志级别或输出配置。
         */
        static void initialize() { horizon::core::initialize_logging(); }

        /**
         * @brief 设置日志级别
         * @param level 最低日志级别
         */
        static void set_log_level(LogLevel level)
        {
            using CoreLevel = horizon::core::LogLevel;
            switch (level)
            {
            case LogLevel::trace:
                horizon::core::set_log_level(CoreLevel::Trace);
                break;
            case LogLevel::debug:
                horizon::core::set_log_level(CoreLevel::Debug);
                break;
            case LogLevel::info:
                horizon::core::set_log_level(CoreLevel::Info);
                break;
            case LogLevel::warning:
                horizon::core::set_log_level(CoreLevel::Warning);
                break;
            case LogLevel::error:
                horizon::core::set_log_level(CoreLevel::Error);
                break;
            case LogLevel::fatal:
                horizon::core::set_log_level(CoreLevel::Critical);
                break;
            default:
                horizon::core::set_log_level(CoreLevel::Info);
                break;
            }
        }

        /**
         * @brief 刷新所有待处理的日志
         *
         * 强制将缓冲区的日志立即写入（通常用于程序退出前）
         */
        static void flush() { horizon::core::log_flush(); }

        /**
         * @brief 获取底层 Quill Logger（高级用户）
         * @return Quill logger 指针，用于直接调用 Quill API
         */
        static quill::Logger* get_logger() { return horizon::core::get_quill_logger(); }

    private:
        CoronaLogger() = delete;
    };

} // namespace Corona::Kernel

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
