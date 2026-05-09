#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // 防止 Windows.h 定义 min/max 宏，避免与 std::numeric_limits 冲突
#endif
#include <Windows.h>
#endif

#include "corona/kernel/core/callback_sink.h"
#include "corona/kernel/core/i_logger.h"
#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/backend/SignalHandler.h"
#include "quill/core/PatternFormatterOptions.h"
#include "quill/sinks/ConsoleSink.h"
#include "quill/sinks/FileSink.h"

namespace Corona::Kernel {

namespace {
// 单例控制
static std::once_flag init_flag;
static quill::Logger* g_logger = nullptr;
static std::shared_ptr<CallbackSink> g_callback_sink = nullptr;

// Corona LogLevel 映射到 Quill LogLevel
quill::LogLevel to_quill_level(LogLevel level) {
    switch (level) {
        case LogLevel::trace:
            return quill::LogLevel::TraceL3;
        case LogLevel::debug:
            return quill::LogLevel::Debug;
        case LogLevel::info:
            return quill::LogLevel::Info;
        case LogLevel::warning:
            return quill::LogLevel::Warning;
        case LogLevel::error:
            return quill::LogLevel::Error;
        case LogLevel::fatal:
            return quill::LogLevel::Critical;
        default:
            return quill::LogLevel::Info;
    }
}

// 生成日志文件名: YYYY-MM-DD_HH-MM-SS_corona.log
std::string generate_log_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d_%H-%M-%S")
        << "_corona.log";

    return oss.str();
}

void initialize_impl() {
#ifdef _WIN32
    // 设置控制台代码页为 UTF-8，确保中文路径等能正确显示
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 启用控制台虚拟终端序列以支持 ANSI 颜色
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif

    // 配置信号处理器选项，确保程序崩溃时日志能够刷新
    quill::SignalHandlerOptions signal_handler_options;
    signal_handler_options.catchable_signals = {SIGTERM, SIGINT, SIGABRT, SIGFPE, SIGILL, SIGSEGV};
    signal_handler_options.timeout_seconds = 20;  // 信号处理超时时间
    signal_handler_options.logger_name = "corona_default";  // 使用的 logger 名称

    // 配置后端选项
    quill::BackendOptions backend_options;
    backend_options.sleep_duration = std::chrono::microseconds{100};
    // 禁用非 ASCII 字符转义，以支持 UTF-8 编码的中文路径等
    // 默认的 check_printable_char 只允许 ASCII 可打印字符，会将中文转为 \xNN
    backend_options.check_printable_char = {};

    // 启动 Quill Backend（带信号处理器）
    quill::Backend::start<quill::FrontendOptions>(backend_options, signal_handler_options);

    // 配置日志格式: [时间戳][线程ID][级别][文件:行号] 消息
    quill::PatternFormatterOptions formatter_options;
    formatter_options.format_pattern = "[%(time)][%(thread_id)][%(log_level)][%(file_name):%(line_number)] %(message)";
    formatter_options.timestamp_pattern = "%Y-%m-%dT%H:%M:%S.%Qns";
    formatter_options.timestamp_timezone = quill::Timezone::LocalTime;

    // 创建控制台 Sink
    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("corona_console");

    // 确保 logs 目录存在
    namespace fs = std::filesystem;
    fs::path log_dir = "logs";
    if (!fs::exists(log_dir)) {
        fs::create_directories(log_dir);
    }

    // 创建文件 Sink (带时间戳的文件名)
    std::string log_filename = (log_dir / generate_log_filename()).string();
    auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(
        log_filename,
        []() {
            quill::FileSinkConfig cfg;
            cfg.set_open_mode('w');  // 每次运行创建新文件
            return cfg;
        }(),
        quill::FileEventNotifier{});

    // 创建 CallbackSink（用于前端日志转发）
    g_callback_sink = std::make_shared<CallbackSink>();

    // 创建 Logger，同时添加控制台、文件和回调三个 sinks
    std::vector<std::shared_ptr<quill::Sink>> sinks;
    sinks.push_back(console_sink);
    sinks.push_back(file_sink);
    sinks.push_back(g_callback_sink);

    g_logger = quill::Frontend::create_or_get_logger(
        "corona_default",
        std::move(sinks),
        formatter_options);

// 设置默认日志级别
#ifndef CORONA_LOG_LEVEL
#ifdef _DEBUG
    g_logger->set_log_level(quill::LogLevel::Debug);
#else
    g_logger->set_log_level(quill::LogLevel::Info);
#endif
#else
    g_logger->set_log_level(to_quill_level(static_cast<LogLevel>(CORONA_LOG_LEVEL)));
#endif
}

}  // namespace

// ========================================
// CoronaLogger 实现
// ========================================

void CoronaLogger::initialize() {
    std::call_once(init_flag, initialize_impl);
}

void CoronaLogger::set_log_level(LogLevel level) {
    initialize();
    if (g_logger) {
        g_logger->set_log_level(to_quill_level(level));
    }
}

void CoronaLogger::flush() {
    if (g_logger) {
        g_logger->flush_log();
    }
}

quill::Logger* CoronaLogger::get_logger() {
    initialize();
    return g_logger;
}

CallbackSink* CoronaLogger::get_callback_sink() {
    initialize();
    return g_callback_sink.get();
}

std::vector<LogEntry> CoronaLogger::drain_logs() {
    if (!g_callback_sink) return {};
    return g_callback_sink->drain();
}

void CoronaLogger::set_callback_sink_level(LogLevel min_level) {
    initialize();
    if (g_callback_sink) {
        g_callback_sink->set_log_level_filter(to_quill_level(min_level));
    }
}

}  // namespace Corona::Kernel
