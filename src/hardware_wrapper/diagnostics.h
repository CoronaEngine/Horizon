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

    void write(Level level, std::string_view channel, std::string_view message) noexcept;
}
