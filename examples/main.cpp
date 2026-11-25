#include "horizon/core/logging.h"
#include "rhi/context.h"

#include "Baseline/EDSL/BaselineEdsl.h"
#include "Baseline/Ocarina/BaselineOcarina.h"
#include "Baseline/Slang/BaselineSlang.h"
#include "Disney/EDSL/DisneyEdsl.h"
#include "Disney/Ocarina/DisneyOcarina.h"
#include "Disney/Slang/DisneySlang.h"
#include "DrawStress/EDSL/DrawStressEdsl.h"
#include "DrawStress/Ocarina/DrawStressOcarina.h"
#include "DrawStress/Slang/DrawStressSlang.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string_view>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <mach-o/dyld.h>
#endif
#include <GLFW/glfw3native.h>

int main(int argc, char **argv)
{
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
    logging.configure_utf8_console = true;
#ifdef CORONA_LOG_LEVEL
    logging.level = static_cast<horizon::core::LogLevel>(CORONA_LOG_LEVEL);
#endif
    horizon::core::initialize_logging(logging);

    constexpr std::string_view default_mode = "baseline_slang";
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : default_mode;

    constexpr std::string_view modes[] = {
        "baseline_edsl", "baseline_slang", "disney_edsl", "disney_slang",
        "drawstress_edsl", "drawstress_slang", "baseline_ocarina", "disney_ocarina", "drawstress_ocarina",
    };
    if (mode == "--list" || mode == "--help" || (argc > 2 && std::string_view(argv[2]) == "--help")) {
        if (mode != "--list") {
            std::cout << "Usage: HorizonExamples [mode] (default: " << default_mode << ")\n";
        }
        for (const auto name : modes) std::cout << name << '\n';
        return 0;
    }
    if (argc > 2 || std::ranges::find(modes, mode) == std::end(modes)) return 2;

    const bool hidden = false;
    const bool offscreen = false;

#if defined(__linux__)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, !hidden);
    constexpr int width = 800, height = 600;
    const char* title = mode.data();
    if (mode == "baseline_edsl") title = "Horizon Baseline [EDSL]";
    if (mode == "baseline_slang") title = "Horizon Baseline [Slang]";
    auto endGlfw = [](GLFWwindow*) { glfwTerminate(); }; // Also destroys the window.
    std::unique_ptr<GLFWwindow, decltype(endGlfw)> window(glfwCreateWindow(width, height, title, nullptr, nullptr), endGlfw);
    if (!window) {
        glfwTerminate();
        return 1;
    }

#if defined(_WIN32)
    void* native_window = glfwGetWin32Window(window.get());
#elif defined(__linux__)
    void* native_window = reinterpret_cast<void*>(glfwGetX11Window(window.get()));
#elif defined(__APPLE__)
    void* native_window = glfwGetCocoaView(window.get());
#endif
    if (offscreen && !mode.starts_with("baseline_") && !mode.ends_with("_ocarina"))
        native_window = nullptr;
    if (mode.ends_with("_ocarina")) {
        // Ocarina loads its backend and Slang builtins from beside the executable.
#if defined(_WIN32)
        std::wstring executable(32768, L'\0');
        executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
        const auto executable_dir = std::filesystem::path(executable).parent_path();
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string executable(size, '\0');
        _NSGetExecutablePath(executable.data(), &size);
        const auto executable_dir = std::filesystem::canonical(executable.c_str()).parent_path();
#elif defined(__linux__)
        const auto executable_dir = std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
        static_cast<void>(ocarina::RHIContext::instance(executable_dir));
    }

    std::unique_ptr<ExampleBase> example;
    if (mode == "baseline_edsl") example = std::make_unique<BaselineEdsl>(native_window);
    else if (mode == "baseline_slang") example = std::make_unique<BaselineSlang>(native_window);
    else if (mode == "disney_edsl") example = std::make_unique<DisneyEdsl>(native_window);
    else if (mode == "disney_slang") example = std::make_unique<DisneySlang>(native_window);
    else if (mode == "drawstress_edsl") example = std::make_unique<DrawStressEdsl>(native_window);
    else if (mode == "drawstress_slang") example = std::make_unique<DrawStressSlang>(native_window);
    else if (mode == "baseline_ocarina") example = std::make_unique<BaselineOcarina>(native_window);
    else if (mode == "disney_ocarina") example = std::make_unique<DisneyOcarina>(native_window);
    else if (mode == "drawstress_ocarina") example = std::make_unique<DrawStressOcarina>(native_window);

    for (;;) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window.get())) break;
        if (native_window && glfwGetWindowAttrib(window.get(), GLFW_ICONIFIED)) {
            glfwWaitEvents();
            continue;
        }
        example->run();
    }
    return 0;
}
