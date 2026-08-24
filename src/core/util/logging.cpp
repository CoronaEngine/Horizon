//
// Created by Zero on 24/04/2022.
//

#include "core/util/logging.h"

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>

namespace horizon::core {
namespace {
quill::Logger *quill_logger() noexcept {
    static quill::Logger *instance = [] {
        quill::Backend::start();
        auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>(
            "horizon_core_console");
        auto *result = quill::Frontend::create_or_get_logger("horizon_core", std::move(sink));
#ifndef NDEBUG
        result->set_log_level(quill::LogLevel::Debug);
#else
        result->set_log_level(quill::LogLevel::Info);
#endif
        return result;
    }();
    return instance;
}
}// namespace

namespace detail {
void log_debug_message(std::string message) noexcept {
    LOG_DEBUG(quill_logger(), "{}", message);
}

void log_info_message(std::string message) noexcept {
    LOG_INFO(quill_logger(), "{}", message);
}

void log_warning_message(std::string message) noexcept {
    LOG_WARNING(quill_logger(), "{}", message);
}

void log_error_message(std::string message) noexcept {
    LOG_ERROR(quill_logger(), "{}", message);
}
}// namespace detail

void log_level_debug() noexcept { quill_logger()->set_log_level(quill::LogLevel::Debug); }
void log_level_info() noexcept { quill_logger()->set_log_level(quill::LogLevel::Info); }
void log_level_warning() noexcept { quill_logger()->set_log_level(quill::LogLevel::Warning); }
void log_level_error() noexcept { quill_logger()->set_log_level(quill::LogLevel::Error); }

void log_flush() noexcept { quill_logger()->flush_log(); }
}// namespace horizon::core
