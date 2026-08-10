//
// Created by Zero on 24/04/2022.
//

#include "core/util/logging.h"

namespace ocarina {
inline namespace core {
spdlog::logger &logger() noexcept {
    static auto logger = [] {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        spdlog::logger l{"console", sink};
        l.flush_on(spdlog::level::err);
#ifndef NDEBUG
        l.set_level(spdlog::level::debug);
#else
        l.set_level(spdlog::level::info);
#endif
        return l;
    }();
    return logger;
}

void log_level_debug() noexcept { logger().set_level(spdlog::level::debug); }
void log_level_info() noexcept { logger().set_level(spdlog::level::info); }
void log_level_warning() noexcept { logger().set_level(spdlog::level::warn); }
void log_level_error() noexcept { logger().set_level(spdlog::level::err); }

void log_flush() noexcept { logger().flush(); }
}
}// namespace ocarina::core