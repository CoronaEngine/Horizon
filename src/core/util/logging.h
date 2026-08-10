//
// Created by Zero on 24/04/2022.
//

#pragma once

#include "core/util/string_util.h"
#include <exception>
#include <iostream>
#include <filesystem>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace ocarina::inline core {
OC_CORE_API spdlog::logger &logger() noexcept;

OC_CORE_API void log_level_debug() noexcept;
OC_CORE_API void log_level_info() noexcept;
OC_CORE_API void log_level_warning() noexcept;
OC_CORE_API void log_level_error() noexcept;

template<typename... Args>
inline void debug(Args &&...args) noexcept {
    logger().debug(serialize(std::forward<Args>(args)...));
}

template<typename... Args>
inline void info(Args &&...args) noexcept {
    logger().info(serialize(std::forward<Args>(args)...));
}

template<typename... Args>
inline void warning(Args &&...args) noexcept {
    logger().warn(serialize(std::forward<Args>(args)...));
}

template<typename... Args>
inline void warning_if(bool predicate, Args &&...args) noexcept {
    if (predicate) { warning(std::forward<Args>(args)...); }
}

template<typename... Args>
inline void warning_if_not(bool predicate, Args &&...args) noexcept {
    warning_if(!predicate, std::forward<Args>(args)...);
}

template<typename... Args>
[[noreturn]] inline void exception(Args &&...args) {
    throw std::runtime_error{serialize(std::forward<Args>(args)...)};
}

template<typename... Args>
inline void exception_if(bool predicate, Args &&...args) {
    if (predicate) { exception(std::forward<Args>(args)...); }
}

template<typename... Args>
inline void exception_if_not(bool predicate, Args &&...args) {
    exception_if(!predicate, std::forward<Args>(args)...);
}

template<typename... Args>
[[noreturn]] inline void error(Args &&...args) {
    logger().error(serialize(std::forward<Args>(args)...));
    OC_ASSERT(0);
    exit(-1);
}

template<typename... Args>
inline void error_if(bool predicate, Args &&...args) {
    if (predicate) { error(std::forward<Args>(args)...); }
}

template<typename... Args>
inline void error_if_not(bool predicate, Args &&...args) {
    error_if(!predicate, std::forward<Args>(args)...);
}
}// namespace ocarina::inline core

#define OC_SOURCE_LOCATION "\n", __FILE__, ":", __LINE__

#define OC_DEBUG(...) \
    ::ocarina::core::debug(__VA_ARGS__)
#define OC_DEBUG_FORMAT(FMT, ...) \
    OC_DEBUG(ocarina::format(FMT, __VA_ARGS__))
#define OC_DEBUG_WITH_LOCATION(...) \
    OC_DEBUG(__VA_ARGS__, OC_SOURCE_LOCATION)
#define OC_DEBUG_FORMAT_WITH_LOCATION(FMT, ...) \
    OC_DEBUG(ocarina::format(FMT, __VA_ARGS__), OC_SOURCE_LOCATION)

#define OC_INFO(...) \
    ::ocarina::core::info(__VA_ARGS__)
#define OC_INFO_FORMAT(FMT, ...) \
    OC_INFO(ocarina::format(FMT, __VA_ARGS__))
#define OC_INFO_WITH_LOCATION(...) \
    OC_INFO(__VA_ARGS__, OC_SOURCE_LOCATION)
#define OC_INFO_FORMAT_WITH_LOCATION(FMT, ...) \
    OC_INFO(ocarina::format(FMT, __VA_ARGS__), OC_SOURCE_LOCATION)

#define OC_WARNING(...) \
    ::ocarina::core::warning(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_WARNING_IF(...) \
    ::ocarina::core::warning_if(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_WARNING_IF_NOT(...) \
    ::ocarina::core::warning_if_not(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_WARNING_FORMAT(FMT, ...) \
    OC_WARNING(ocarina::format(FMT, __VA_ARGS__));

#define OC_EXCEPTION(...) \
    ::ocarina::core::exception(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_EXCEPTION_IF(...) \
    ::ocarina::core::exception_if(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_EXCEPTION_IF_NOT(...) \
    ::ocarina::core::exception_if_not(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_EXCEPTION_FORMAT(FMT, ...) \
    OC_EXCEPTION(ocarina::format(FMT, __VA_ARGS__))

#define OC_ERROR(...) \
    ::ocarina::core::error(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_ERROR_IF(...) \
    ::ocarina::core::error_if(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_ERROR_IF_NOT(...) \
    ::ocarina::core::error_if_not(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_ERROR_FORMAT(FMT, ...) \
    OC_ERROR(ocarina::format(FMT, __VA_ARGS__));

#define OC_NOT_IMPLEMENT_ERROR(func_name) OC_ERROR(#func_name " is not implemented")
