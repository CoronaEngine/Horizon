//
// Created by Zero on 24/04/2022.
//

#pragma once

#include <string>
#include <string_view>
#include <sstream>
#include <fstream>
#include <array>
#include <codecvt>
#include <regex>
#include <filesystem>
#include <fmt/format.h>
#include "math/basic_types.h"
#include "core/stl.h"

namespace ocarina::inline core {
template<typename... Args>
inline std::string serialize(Args &&...args) noexcept {
    std::ostringstream ss;
    static_cast<void>((ss << ... << std::forward<Args>(args)));
    return ss.str();
}

inline std::string text_file_contents(const std::filesystem::path &file_path) {
    std::ifstream file{file_path};
    if (!file.is_open()) {
        std::ostringstream ss;
        ss << "Failed to open file: " << file_path;
        throw std::runtime_error{ss.str()};
    }
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

inline std::string to_lower(std::string s) noexcept {
    for (auto &&c : s) { c = static_cast<char>(std::tolower(c)); }
    return s;
}

inline std::string to_upper(std::string s) noexcept {
    for (auto &&c : s) { c = static_cast<char>(std::toupper(c)); }
    return s;
}

inline std::string jsonc_to_json(const std::string &jsonc) {
    const char regex_exprs[][64] = {
        R"((?=[\r\n])\s*\/\/.*)",                               // Single line comment
        R"((\".+?(?!\\\\)\"[\{\}\[\]:,\s]*?)\/\/.*?(?=[\r\n]))",// Single line comment in the line end
                                                                // Mutiple line comment is trick, we can not handle it.
    };
    std::string json = std::regex_replace(jsonc, std::regex(regex_exprs[0]), "");
    json = std::regex_replace(json, std::regex(regex_exprs[1]), "$1");
    return json;
}

inline void string_printf_recursive(std::string *s, const char *fmt) {
    const char *c = fmt;
    // No args left; make sure there aren't any extra formatting
    // specifiers.
    while (*c) {
        if (*c == '%') {
            ++c;
        }
        *s += *c++;
    }
}

// 1. Copy from fmt to *s, up to the next formatting directive.
// 2. Advance fmt past the next formatting directive and return the
//    formatting directive as a string.
inline std::string copy_to_format_string(const char **fmt_ptr, std::string *s) {
    const char *&fmt = *fmt_ptr;
    while (*fmt) {
        if (*fmt != '%') {
            *s += *fmt;
            ++fmt;
        } else if (fmt[1] == '%') {
            // "%%"; let it pass through
            *s += '%';
            *s += '%';
            fmt += 2;
        } else
            // fmt is at the start of a formatting directive.
            break;
    }

    std::string nextFmt;
    if (*fmt) {
        do {
            nextFmt += *fmt;
            ++fmt;
            // Incomplete (but good enough?) test for the end of the
            // formatting directive: a new formatting directive starts, we
            // hit whitespace, or we hit a comma.
        } while (*fmt && *fmt != '%' && !isspace(*fmt) && *fmt != ',' &&
                 *fmt != '[' && *fmt != ']' && *fmt != '(' && *fmt != ')');
    }

    return nextFmt;
}

template<typename T>
inline std::string format_one(const char *fmt, T v) {
    // Figure out how much space we need to allocate; add an extra
    // character for the '\0'.
    size_t size = snprintf(nullptr, 0, fmt, v) + 1;
    std::string str;
    str.resize(size);
    snprintf(&str[0], size, fmt, v);
    str.pop_back();// remove trailing NUL
    return str;
}

// General-purpose version of string_printf_recursive; add the formatted
// output for a single string_printf() argument to the final result string
// in *s.
template<typename T, typename... Args>
inline void string_printf_recursive(std::string *s, const char *fmt, T v,
                                    Args... args) {
    std::string nextFmt = copy_to_format_string(&fmt, s);
    *s += format_one(nextFmt.c_str(), v);
    string_printf_recursive(s, fmt, args...);
}

// Special case of StringPrintRecursive for float-valued arguments.
template<typename... Args>
inline void string_printf_recursive(std::string *s, const char *fmt, float v,
                                    Args... args) {
    std::string nextFmt = copy_to_format_string(&fmt, s);
    if (nextFmt == "%f")
        // Always use enough precision so that the printed value gives
        // the exact floating-point value if it's used to initialize a
        // float.
        // https://randomascii.wordpress.com/2012/03/08/float-precisionfrom-zero-to-100-digits-2/
        *s += format_one("%.9g", v);
    else
        // If a specific formatting string other than "%f" was specified,
        // just use that.
        *s += format_one(nextFmt.c_str(), v);

    // Go forth and print the next arg.
    string_printf_recursive(s, fmt, args...);
}

// Specialization for doubles that always uses enough precision.  (It seems
// that this is the version that is actually called for floats.  I thought
// that float->double promotion wasn't supposed to happen in this case?)
template<typename... Args>
inline void string_printf_recursive(std::string *s, const char *fmt, double v,
                                    Args... args) {
    std::string nextFmt = copy_to_format_string(&fmt, s);
    if (nextFmt == "%f")
        *s += format_one("%.17g", v);
    else
        *s += format_one(nextFmt.c_str(), v);
    string_printf_recursive(s, fmt, args...);
}

// string_printf() is a replacement for sprintf() (and the like) that
// returns the result as a std::string. This gives convenience/control
// of printf-style formatting in a more C++-ish way.
//
// Floating-point values with the formatting string "%f" are handled
// specially so that enough digits are always printed so that the original
// float/double can be reconstituted exactly from the printed digits.
template<typename... Args>
inline std::string string_printf(const char *fmt, Args... args) {
    std::string ret;
    string_printf_recursive(&ret, fmt, args...);
    return ret;
}

template<typename FMT, typename... Args>
[[nodiscard]] inline auto format(FMT &&f, Args &&...args) noexcept {
    using memory_buffer = fmt::basic_memory_buffer<char, fmt::inline_buffer_size, ocarina::allocator<char>>;
    memory_buffer buffer;
    fmt::format_to(std::back_inserter(buffer),
                   fmt::runtime(std::forward<FMT>(f)),
                   std::forward<Args>(args)...);
    return ocarina::string{buffer.data(), buffer.size()};
}

[[nodiscard]] inline auto string_split(ocarina::string_view str, char ch) {
    ocarina::vector<ocarina::string_view> ret;
    int prev_cursor = 0;
    for (int i = 0; i < str.size(); ++i) {
        if (str[i] == ch) {
            ret.push_back(str.substr(prev_cursor, i - prev_cursor));
            prev_cursor = i + 1;
        }
    }
    ret.push_back(str.substr(prev_cursor, str.size() - prev_cursor));
    return ret;
}

[[nodiscard]] inline std::string string_replace(std::string str, const std::string_view &from,
                                                const std::string_view &to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

[[nodiscard]] inline bool string_contains(const std::string_view &string,
                                          const std::string_view &sub) {
    return string.find(sub) != std::string::npos;
}

[[nodiscard]] inline int string_count(const std::string &str, const std::string &substr) {
    int count = 0;
    size_t pos = str.find(substr);
    while (pos != std::string::npos) {
        ++count;
        pos = str.find(substr, pos + substr.length());
    }
    return count;
}

[[nodiscard]] inline std::string from_file(const fs::path &fn) {
    std::ifstream fst;
    fst.open(fn.c_str());
    std::stringstream buffer;
    buffer << fst.rdbuf();
    std::string str = buffer.str();
    return str;
}
}// namespace ocarina::inline core
