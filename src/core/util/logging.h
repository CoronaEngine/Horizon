//
// Created by Zero on 24/04/2022.
//

#pragma once

#include "core/util/string_util.h"

#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace horizon::core {
namespace detail {
OC_CORE_API void log_debug_message(std::string message) noexcept;
OC_CORE_API void log_info_message(std::string message) noexcept;
OC_CORE_API void log_warning_message(std::string message) noexcept;
OC_CORE_API void log_error_message(std::string message) noexcept;
}// namespace detail

OC_CORE_API void log_level_debug() noexcept;
OC_CORE_API void log_level_info() noexcept;
OC_CORE_API void log_level_warning() noexcept;
OC_CORE_API void log_level_error() noexcept;
OC_CORE_API void log_flush() noexcept;

template<typename... Args>
inline void debug(Args &&...args) noexcept {
    detail::log_debug_message(serialize(std::forward<Args>(args)...));
}

template<typename... Args>
inline void info(Args &&...args) noexcept {
    detail::log_info_message(serialize(std::forward<Args>(args)...));
}

template<typename... Args>
inline void warning(Args &&...args) noexcept {
    detail::log_warning_message(serialize(std::forward<Args>(args)...));
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
    detail::log_error_message(serialize(std::forward<Args>(args)...));
    log_flush();
    OC_ASSERT(0);
    std::exit(-1);
}

template<typename... Args>
inline void error_if(bool predicate, Args &&...args) {
    if (predicate) { error(std::forward<Args>(args)...); }
}

template<typename... Args>
inline void error_if_not(bool predicate, Args &&...args) {
    error_if(!predicate, std::forward<Args>(args)...);
}
}// namespace horizon::core

#define OC_SOURCE_LOCATION "\n", __FILE__, ":", __LINE__

#define OC_DEBUG(...) \
    ::horizon::core::debug(__VA_ARGS__)
#define OC_DEBUG_FORMAT(FMT, ...) \
    OC_DEBUG(horizon::core::format(FMT, __VA_ARGS__))
#define OC_DEBUG_WITH_LOCATION(...) \
    OC_DEBUG(__VA_ARGS__, OC_SOURCE_LOCATION)
#define OC_DEBUG_FORMAT_WITH_LOCATION(FMT, ...) \
    OC_DEBUG(horizon::core::format(FMT, __VA_ARGS__), OC_SOURCE_LOCATION)

#define OC_INFO(...) \
    ::horizon::core::info(__VA_ARGS__)
#define OC_INFO_FORMAT(FMT, ...) \
    OC_INFO(horizon::core::format(FMT, __VA_ARGS__))
#define OC_INFO_WITH_LOCATION(...) \
    OC_INFO(__VA_ARGS__, OC_SOURCE_LOCATION)
#define OC_INFO_FORMAT_WITH_LOCATION(FMT, ...) \
    OC_INFO(horizon::core::format(FMT, __VA_ARGS__), OC_SOURCE_LOCATION)

#define OC_WARNING(...) \
    ::horizon::core::warning(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_WARNING_IF(...) \
    ::horizon::core::warning_if(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_WARNING_IF_NOT(...) \
    ::horizon::core::warning_if_not(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_WARNING_FORMAT(FMT, ...) \
    OC_WARNING(horizon::core::format(FMT, __VA_ARGS__));

#define OC_EXCEPTION(...) \
    ::horizon::core::exception(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_EXCEPTION_IF(...) \
    ::horizon::core::exception_if(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_EXCEPTION_IF_NOT(...) \
    ::horizon::core::exception_if_not(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_EXCEPTION_FORMAT(FMT, ...) \
    OC_EXCEPTION(horizon::core::format(FMT, __VA_ARGS__))

#define OC_ERROR(...) \
    ::horizon::core::error(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_ERROR_IF(...) \
    ::horizon::core::error_if(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_ERROR_IF_NOT(...) \
    ::horizon::core::error_if_not(__VA_ARGS__, "\n    Source: ", OC_SOURCE_LOCATION)
#define OC_ERROR_FORMAT(FMT, ...) \
    OC_ERROR(horizon::core::format(FMT, __VA_ARGS__));

#define OC_NOT_IMPLEMENT_ERROR(func_name) OC_ERROR(#func_name " is not implemented")
