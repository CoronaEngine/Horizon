#pragma once

#include <string>
#include <string_view>

namespace Corona::Horizon::Diagnostics
{
    enum class Level
    {
        Info,
        Warning,
        Error
    };

    [[nodiscard]] bool enabled() noexcept;
    [[nodiscard]] std::string file_path();

    void reset_for_tests() noexcept;
    void write(Level level, std::string_view channel, std::string_view message) noexcept;
}
