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
            throw std::invalid_argument("--frames requires a positive integer.");
        }

        for (char ch : value)
        {
            if (ch < '0' || ch > '9')
            {
                throw std::invalid_argument("--frames requires a positive integer.");
            }
            result = result * 10 + static_cast<uint64_t>(ch - '0');
            if (result > UINT32_MAX)
            {
                throw std::out_of_range("--frames exceeds uint32_t.");
            }
        }

        return static_cast<uint32_t>(result);
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
}

int main(int argc, char** argv)
{
    uint32_t frames = 180;
    std::string_view example = "default";
    ExampleDefaultThreadMode thread_mode = ExampleDefaultThreadMode::SingleThreaded;

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

            if (arg == "default")
            {
                example = "default";
                continue;
            }

            throw std::invalid_argument("Unknown HorizonExamples argument: " + std::string(arg));
        }

        if (example == "default")
        {
            run_example_default(frames, thread_mode);
            return 0;
        }

        throw std::invalid_argument("Unknown Horizon example: " + std::string(example));
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
