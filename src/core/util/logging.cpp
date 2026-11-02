//
// Created by Zero on 24/04/2022.
//

#include "core/util/logging.h"

namespace ocarina {
inline namespace core {
quill::Logger *logger() noexcept {
    return horizon::core::get_quill_logger();
}

void log_message(LogLevel level, std::string_view message) noexcept {
    switch (level) {
        case LogLevel::Trace: LOG_TRACE_L3(logger(), "{}", message); break;
        case LogLevel::Debug: LOG_DEBUG(logger(), "{}", message); break;
        case LogLevel::Info: LOG_INFO(logger(), "{}", message); break;
        case LogLevel::Warning: LOG_WARNING(logger(), "{}", message); break;
        case LogLevel::Error: LOG_ERROR(logger(), "{}", message); break;
        case LogLevel::Critical: LOG_CRITICAL(logger(), "{}", message); break;
    }
}

void log_level_debug() noexcept { horizon::core::set_log_level(LogLevel::Debug); }
void log_level_info() noexcept { horizon::core::set_log_level(LogLevel::Info); }
void log_level_warning() noexcept { horizon::core::set_log_level(LogLevel::Warning); }
void log_level_error() noexcept { horizon::core::set_log_level(LogLevel::Error); }

void log_flush() noexcept { horizon::core::log_flush(); }
}
}// namespace ocarina::core
