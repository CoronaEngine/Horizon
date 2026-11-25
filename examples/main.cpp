#include "ExampleBase.h"
#include "Baseline/EDSL/BaselineEdsl.h"
#include "Baseline/Ocarina/BaselineOcarina.h"
#include "Baseline/Slang/BaselineSlang.h"
#include "Disney/EDSL/DisneyEdsl.h"
#include "Disney/Ocarina/DisneyOcarina.h"
#include "Disney/Slang/DisneySlang.h"
#include "DrawStress/EDSL/DrawStressEdsl.h"
#include "DrawStress/Ocarina/DrawStressOcarina.h"
#include "DrawStress/Slang/DrawStressSlang.h"
#include "rhi/context.h"

#include <filesystem>
#include <memory>
#include <string>
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

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "baseline_slang";

#if defined(__linux__)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (glfwInit()) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        GLFWwindow* window = glfwCreateWindow(800, 600, mode.data(), nullptr, nullptr);

#if defined(_WIN32)
        void* native_window = glfwGetWin32Window(window);
#elif defined(__linux__)
        void* native_window = reinterpret_cast<void*>(glfwGetX11Window(window));
#elif defined(__APPLE__)
        void* native_window = glfwGetCocoaView(window);
#endif

        if (mode.ends_with("_ocarina")) {
            // Ocarina loads its backend and Slang builtins from beside the executable.
#if defined(_WIN32)
            std::wstring executable(32768, L'\0');
            executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
#elif defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::string path(size, '\0');
            _NSGetExecutablePath(path.data(), &size);
            const auto executable = std::filesystem::canonical(path.c_str());
#elif defined(__linux__)
            const auto executable = std::filesystem::read_symlink("/proc/self/exe");
#endif
            static_cast<void>(ocarina::RHIContext::instance(std::filesystem::path(executable).parent_path()));
        }

        {
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
            else example = std::make_unique<BaselineEdsl>(native_window);

            while (!glfwWindowShouldClose(window)) {
                example->run();
                glfwPollEvents();
            }
        }

        glfwTerminate();
    }

    return 0;
}
