#include "diagnostics.h"

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>

namespace Corona::Horizon::Diagnostics
{
    namespace
    {
#if HORIZON_ENABLE_VALIDATION && defined(CABBAGE_ENGINE_DEBUG)
        constexpr bool diagnostics_compiled = true;
#else
        constexpr bool diagnostics_compiled = false;
#endif

        struct DiagnosticState
        {
            std::mutex mutex;
            bool initialized { false };
            bool disabled { false };
            std::string path;
        };

        DiagnosticState& state() noexcept
        {
            static DiagnosticState value;
            return value;
        }

        [[nodiscard]] const char* level_name(Level level) noexcept
        {
            switch (level)
            {
            case Level::Info:
                return "INFO";
            case Level::Warning:
                return "WARNING";
            case Level::Error:
                return "ERROR";
            }

            return "INFO";
        }

        [[nodiscard]] std::string resolve_path()
        {
            if (const char* override_path = std::getenv("HORIZON_VULKAN_DIAGNOSTICS_PATH");
                override_path != nullptr && override_path[0] != '\0')
            {
                return override_path;
            }

            return "horizon-vulkan-diagnostics.txt";
        }

        void initialize_locked(DiagnosticState& current) noexcept
        {
            if (!diagnostics_compiled || current.initialized)
            {
                return;
            }

            current.path = resolve_path();
            current.initialized = true;

            try
            {
                std::ofstream stream(current.path, std::ios::out | std::ios::trunc);
                if (!stream)
                {
                    current.disabled = true;
                    return;
                }

                stream << "Horizon Vulkan Diagnostics\n";
                stream << "Path override: HORIZON_VULKAN_DIAGNOSTICS_PATH\n\n";
            }
            catch (...)
            {
                current.disabled = true;
            }
        }
    }

    bool enabled() noexcept
    {
        return diagnostics_compiled;
    }

    std::string file_path()
    {
        if (!diagnostics_compiled)
        {
            return {};
        }

        DiagnosticState& current = state();
        std::lock_guard lock(current.mutex);
        initialize_locked(current);
        return current.disabled ? std::string {} : current.path;
    }

    void reset_for_tests() noexcept
    {
        if (!diagnostics_compiled)
        {
            return;
        }

        DiagnosticState& current = state();
        std::lock_guard lock(current.mutex);
        current.initialized = false;
        current.disabled = false;
        current.path.clear();
    }

    void write(Level level, std::string_view channel, std::string_view message) noexcept
    {
        if (!diagnostics_compiled)
        {
            return;
        }

        DiagnosticState& current = state();
        std::lock_guard lock(current.mutex);
        initialize_locked(current);

        if (current.disabled)
        {
            return;
        }

        try
        {
            std::ofstream stream(current.path, std::ios::out | std::ios::app);
            if (!stream)
            {
                current.disabled = true;
                return;
            }

            stream << '[' << level_name(level) << "][" << channel << "] ";
            stream.write(message.data(), static_cast<std::streamsize>(message.size()));
            stream << "\n";
        }
        catch (...)
        {
            current.disabled = true;
        }
    }
}
