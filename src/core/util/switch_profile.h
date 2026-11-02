#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>

// Opt-in, flushed wall-clock events shared by the renderer and its backend.
namespace ocarina::switch_profile {
inline bool enabled() noexcept {
    static const bool value = [] {
        const char* env = std::getenv("CORONA_SWITCH_PROFILE");
        return env && std::string(env) == "1";
    }();
    return value;
}
inline std::string quoted(const char* value) {
    std::string result = "\"";
    for (const unsigned char c : std::string(value)) {
        if (c == '"' || c == '\\') result += '\\';
        if (c >= 32) result += static_cast<char>(c);
        else result += '?';
    }
    return result + '"';
}
// Keep a named instance in the measured function or block. Destruction closes
// the event on normal return, early return and exception unwinding.
class [[nodiscard]] Scope {
    const char* name_;
    const char* category_;
    const bool active_;
    void emit(char phase) const noexcept {
        try {
            const auto time = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const auto thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
            const auto line = std::string("CORONA_PROFILE {\"ph\":\"") + phase +
                "\",\"name\":" + quoted(name_) + ",\"cat\":" + quoted(category_) +
                ",\"ts\":" + std::to_string(time) + ",\"tid\":" + std::to_string(thread) + "}\n";
            std::fwrite(line.data(), 1, line.size(), stdout);
            std::fflush(stdout);
        } catch (...) {
            // Profiling must not change the renderer's error behavior.
        }
    }
public:
    Scope(const char* name, const char* category) noexcept
        : name_(name), category_(category), active_(enabled()) {
        if (active_) emit('B');
    }
    ~Scope() noexcept {
        if (active_) emit('E');
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
} // namespace ocarina::switch_profile
