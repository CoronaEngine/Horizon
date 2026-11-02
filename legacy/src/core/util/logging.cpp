//
// Created by Zero on 24/04/2022.
//

#include "core/util/logging.h"
#include "core/util/logging_quill.h"

#include <chrono>
#include <csignal>
#include <mutex>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/FileSink.h>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace horizon::core {
namespace {
std::once_flag init_flag;
quill::Logger *logger = nullptr;

quill::LogLevel to_quill_level(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::Trace:
            return quill::LogLevel::TraceL3;
        case LogLevel::Debug:
            return quill::LogLevel::Debug;
        case LogLevel::Info:
            return quill::LogLevel::Info;
        case LogLevel::Warning:
            return quill::LogLevel::Warning;
        case LogLevel::Error:
            return quill::LogLevel::Error;
        case LogLevel::Critical:
            return quill::LogLevel::Critical;
        default:
            return quill::LogLevel::Info;
    }
}
}// namespace

void initialize_logging(const LoggingOptions &options)
{
    std::call_once(
        init_flag,
        [&]
        {
            if (!options.console && options.file_path.empty())
            {
                throw std::invalid_argument{"Logging requires a console or file output"};
            }

            std::vector<std::shared_ptr<quill::Sink>> sinks;
            if (options.console)
            {
                quill::ConsoleSinkConfig console_config;
                console_config.set_colour_mode(options.configure_utf8_console
                                                   ? quill::ConsoleSinkConfig::ColourMode::Automatic
                                                   : quill::ConsoleSinkConfig::ColourMode::Never);
                sinks.push_back(
                    quill::Frontend::create_or_get_sink<quill::ConsoleSink>("horizon_core_console", console_config));
            }
            if (!options.file_path.empty())
            {
                // Quill keys sinks by name, so a relative filename must not
                // collide with the console sink's internal identifier.
                const auto file_path = std::filesystem::absolute(options.file_path).lexically_normal();
                std::filesystem::create_directories(file_path.parent_path());
                quill::FileSinkConfig file_config;
                file_config.set_open_mode('w');
                sinks.push_back(quill::Frontend::create_or_get_sink<quill::FileSink>(file_path.string(), file_config,
                                                                                     quill::FileEventNotifier{}));
            }

#ifdef _WIN32
            if (options.console && options.configure_utf8_console)
            {
                SetConsoleOutputCP(CP_UTF8);
                SetConsoleCP(CP_UTF8);
                const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
                DWORD mode = 0;
                if (output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode))
                {
                    SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
                }
            }
#endif

            quill::PatternFormatterOptions formatter;
            formatter.format_pattern = "[%(time)][%(thread_id)][%(log_level)][%(file_name):%(line_number)] %(message)";
            formatter.timestamp_pattern = "%Y-%m-%dT%H:%M:%S.%Qns";
            formatter.timestamp_timezone = quill::Timezone::LocalTime;
            auto *instance = quill::Frontend::create_or_get_logger("horizon_core", std::move(sinks), formatter);
            instance->set_log_level(to_quill_level(options.level));

            quill::BackendOptions backend_options;
            backend_options.sleep_duration = std::chrono::microseconds{100};
            backend_options.check_printable_char = {};
            if (options.install_signal_handlers)
            {
                quill::SignalHandlerOptions signals;
                signals.catchable_signals = {SIGTERM, SIGINT, SIGABRT, SIGFPE, SIGILL, SIGSEGV};
                signals.timeout_seconds = 20;
                signals.logger_name = "horizon_core";
                quill::Backend::start<quill::FrontendOptions>(backend_options, signals);
            }
            else
            {
                quill::Backend::start(backend_options);
            }
            logger = instance;
        });
}

quill::Logger *get_quill_logger()
{
    initialize_logging();
    return logger;
}

namespace detail {
void log_debug_message(std::string message) noexcept {
    LOG_DEBUG(get_quill_logger(), "{}", message);
}

void log_info_message(std::string message) noexcept {
    LOG_INFO(get_quill_logger(), "{}", message);
}

void log_warning_message(std::string message) noexcept {
    LOG_WARNING(get_quill_logger(), "{}", message);
}

void log_error_message(std::string message) noexcept {
    LOG_ERROR(get_quill_logger(), "{}", message);
}
}// namespace detail

void set_log_level(LogLevel level) noexcept
{
    get_quill_logger()->set_log_level(to_quill_level(level));
}
void log_level_debug() noexcept
{
    set_log_level(LogLevel::Debug);
}
void log_level_info() noexcept
{
    set_log_level(LogLevel::Info);
}
void log_level_warning() noexcept
{
    set_log_level(LogLevel::Warning);
}
void log_level_error() noexcept
{
    set_log_level(LogLevel::Error);
}

void log_flush() noexcept
{
    get_quill_logger()->flush_log();
}
}// namespace horizon::core
