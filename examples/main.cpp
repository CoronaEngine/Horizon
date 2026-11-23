#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <exception>
#include <stdexcept>

#include "horizon/core/logging.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

void run_example_baseline_tutorial();
void run_example_baseline_edsl();
void run_example_baseline_slang();
void run_example_disney_edsl();
void run_example_disney_slang();
void run_example_drawstress_edsl();
void run_example_drawstress_slang();

void run_example_baseline_ocarina();
void run_example_disney_ocarina();
void run_example_drawstress_ocarina();

int main(int argc, char **argv)
{
    // Application policy: keep the timestamped file and crash flushing enabled.
    const auto now = std::time(nullptr);
    std::tm local_time {};
#ifdef _WIN32
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    std::ostringstream log_filename;
    log_filename << std::put_time(&local_time, "%Y-%m-%d_%H-%M-%S") << "_corona.log";
    horizon::core::LoggingOptions logging;
    logging.file_path = std::filesystem::path { "logs" } / log_filename.str();
    logging.install_signal_handlers = true;
    logging.configure_utf8_console = true;
#ifdef CORONA_LOG_LEVEL
    logging.level = static_cast<horizon::core::LogLevel>(CORONA_LOG_LEVEL);
#endif
    horizon::core::initialize_logging(logging);

    constexpr std::string_view default_mode = "baseline";
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : default_mode;

    struct Example { std::string_view name; void (*run)(); };
    static constexpr Example examples[] = {
        {"baseline", run_example_baseline_tutorial},
        {"baseline_edsl", run_example_baseline_edsl},
        {"baseline_slang", run_example_baseline_slang},
        {"disney_edsl", run_example_disney_edsl},
        {"disney_slang", run_example_disney_slang},
        {"drawstress_edsl", run_example_drawstress_edsl},
        {"drawstress_slang", run_example_drawstress_slang},
        {"baseline_ocarina", run_example_baseline_ocarina},
        {"disney_ocarina", run_example_disney_ocarina},
        {"drawstress_ocarina", run_example_drawstress_ocarina},
    };
    if (mode == "--list" || mode == "--help" || (argc > 2 && std::string_view(argv[2]) == "--help")) {
        if (mode != "--list") std::cout << "Usage: HorizonExamples [mode] (default: " << default_mode << ")\n";
        for (const auto& example : examples) std::cout << example.name << '\n';
        return 0;
    }
    for (const auto& example : examples) {
        if (example.name != mode) continue;
        try {
            if (mode == "baseline") {
                // The upstream tutorial owns its window. GLFW retains hints across repeated initialization.
                if (!glfwInit()) throw std::runtime_error("glfwInit failed");
                glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
            }
            example.run();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << mode << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cerr << "Unknown example " << mode << ". Use --list to see available modes.\n";
    return 2;
}
