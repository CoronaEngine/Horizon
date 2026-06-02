#include "example_default/example_default.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    [[nodiscard]] uint32_t parse_frame_count(std::string_view value)
    {
        uint64_t result = 0;
        if (value.empty())
        {
            throw std::invalid_argument("--frames requires a non-negative integer.");
        }

        for (char ch : value)
        {
            if (ch < '0' || ch > '9')
            {
                throw std::invalid_argument("--frames requires a non-negative integer.");
            }
            result = result * 10 + static_cast<uint64_t>(ch - '0');
            if (result > UINT32_MAX)
            {
                throw std::out_of_range("--frames exceeds uint32_t.");
            }
        }

        return static_cast<uint32_t>(result);
    }

    [[nodiscard]] uint32_t parse_positive_count(std::string_view value, std::string_view label)
    {
        const uint32_t result = parse_frame_count(value);
        if (result == 0)
        {
            throw std::invalid_argument(std::string(label) + " requires a positive integer.");
        }
        return result;
    }

    [[nodiscard]] ExampleDefaultThreadMode parse_thread_mode(std::string_view value)
    {
        if (value == "single" || value == "single-threaded")
        {
            return ExampleDefaultThreadMode::SingleThreaded;
        }

        if (value == "mesh-render-display")
        {
            return ExampleDefaultThreadMode::MeshRenderDisplay;
        }

        throw std::invalid_argument("Unknown --threads mode: " + std::string(value));
    }

    [[nodiscard]] ExampleDefaultMode parse_example_mode(std::string_view value)
    {
        if (value == "default")
        {
            return ExampleDefaultMode::Default;
        }
        if (value == "glsl")
        {
            return ExampleDefaultMode::Glsl;
        }
        if (value == "edsl")
        {
            return ExampleDefaultMode::Edsl;
        }
        if (value == "texture")
        {
            return ExampleDefaultMode::Texture;
        }
        if (value == "compute")
        {
            return ExampleDefaultMode::Compute;
        }
        if (value == "multi-window")
        {
            return ExampleDefaultMode::MultiWindow;
        }
        if (value == "stress")
        {
            return ExampleDefaultMode::Stress;
        }

        throw std::invalid_argument("Unknown Horizon example: " + std::string(value));
    }
}

int main(int argc, char** argv)
{
    uint32_t frames = 0;
    ExampleDefaultMode mode = ExampleDefaultMode::Default;
    ExampleDefaultThreadMode thread_mode = ExampleDefaultThreadMode::SingleThreaded;
    ExampleDefaultStressConfig stress_config;
    bool stress_config_used = false;

    try
    {
        for (int index = 1; index < argc; ++index)
        {
            std::string_view arg = argv[index] == nullptr ? std::string_view {} : std::string_view { argv[index] };
            if (arg == "--frames")
            {
                if (index + 1 >= argc)
                {
                    throw std::invalid_argument("--frames requires a value.");
                }
                frames = parse_frame_count(argv[++index]);
                continue;
            }

            if (arg == "--threads")
            {
                if (index + 1 >= argc)
                {
                    throw std::invalid_argument("--threads requires a value.");
                }
                thread_mode = parse_thread_mode(argv[++index]);
                continue;
            }

            if (arg == "--windows")
            {
                if (index + 1 >= argc)
                {
                    throw std::invalid_argument("--windows requires a value.");
                }
                stress_config.window_count = parse_positive_count(argv[++index], "--windows");
                stress_config_used = true;
                continue;
            }

            if (arg == "--render-threads")
            {
                if (index + 1 >= argc)
                {
                    throw std::invalid_argument("--render-threads requires a value.");
                }
                stress_config.render_thread_count = parse_positive_count(argv[++index], "--render-threads");
                stress_config_used = true;
                continue;
            }

            if (!arg.starts_with("--"))
            {
                mode = parse_example_mode(arg);
                continue;
            }

            throw std::invalid_argument("Unknown HorizonExamples argument: " + std::string(arg));
        }

        if (mode != ExampleDefaultMode::Default && thread_mode != ExampleDefaultThreadMode::SingleThreaded)
        {
            throw std::invalid_argument("--threads is currently supported only by the default example mode.");
        }

        if (mode != ExampleDefaultMode::Stress && stress_config_used)
        {
            throw std::invalid_argument("--windows and --render-threads are currently supported only by the stress example mode.");
        }

        run_example_default(frames, thread_mode, mode, stress_config);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
