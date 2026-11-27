#include "ExampleBase.h"
#include "Baseline/EDSL/BaselineEdsl.h"
#include "Baseline/Mesh/BaselineMesh.h"
#include "Baseline/RayTrace/BaselineRayTrace.h"
#include "Baseline/Ocarina/BaselineOcarina.h"
#include "Baseline/Slang/BaselineSlang.h"
#include "Disney/EDSL/DisneyEdsl.h"
#include "Disney/Mesh/DisneyMesh.h"
#include "Disney/RayTrace/DisneyRayTrace.h"
#include "Disney/Ocarina/DisneyOcarina.h"
#include "Disney/Slang/DisneySlang.h"
#include "DrawStress/EDSL/DrawStressEdsl.h"
#include "DrawStress/Mesh/DrawStressMesh.h"
#include "DrawStress/RayTrace/DrawStressRayTrace.h"
#include "DrawStress/Ocarina/DrawStressOcarina.h"
#include "DrawStress/Slang/DrawStressSlang.h"
#include "Feature/EdslFeature.h"

#include <format>
#include <memory>
#include <string_view>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "baseline_slang";

    if (glfwInit()) {
#if defined(__linux__)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
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

        {
            std::unique_ptr<ExampleBase> example;
            if (mode == "baseline_edsl") example = std::make_unique<BaselineEdsl>(native_window);
            else if (mode == "baseline_slang") example = std::make_unique<BaselineSlang>(native_window);
            else if (mode == "baseline_mesh") example = std::make_unique<BaselineMesh>(native_window);
            else if (mode == "baseline_raytrace") example = std::make_unique<BaselineRayTrace>(native_window);
            else if (mode == "disney_edsl") example = std::make_unique<DisneyEdsl>(native_window);
            else if (mode == "disney_slang") example = std::make_unique<DisneySlang>(native_window);
            else if (mode == "disney_mesh") example = std::make_unique<DisneyMesh>(native_window);
            else if (mode == "disney_raytrace") example = std::make_unique<DisneyRayTrace>(native_window);
            else if (mode == "drawstress_edsl") example = std::make_unique<DrawStressEdsl>(native_window);
            else if (mode == "drawstress_slang") example = std::make_unique<DrawStressSlang>(native_window);
            else if (mode == "drawstress_mesh") example = std::make_unique<DrawStressMesh>(native_window);
            else if (mode == "drawstress_raytrace") example = std::make_unique<DrawStressRayTrace>(native_window);
            else if (mode == "edsl_feature") example = std::make_unique<EdslFeature>(native_window);
            else if (mode == "baseline_ocarina") example = std::make_unique<BaselineOcarina>(native_window);
            else if (mode == "disney_ocarina") example = std::make_unique<DisneyOcarina>(native_window);
            else if (mode == "drawstress_ocarina") example = std::make_unique<DrawStressOcarina>(native_window);
            else example = std::make_unique<BaselineEdsl>(native_window);

            constexpr double fixed_dt = 0.0;
            double seconds = 0.0;
            double frame_start = glfwGetTime();
            while (!glfwWindowShouldClose(window)) {
                example->run(static_cast<float>(seconds));
                glfwPollEvents();
                const double now = glfwGetTime();
                const double dt = now - frame_start;
                frame_start = now;
                seconds += fixed_dt > 0.0 ? fixed_dt : dt;
                const double fps = dt > 0.0 ? 1.0 / dt : 0.0;
                const auto title = std::format("{} | {:.1f} FPS | {:.2f} ms", mode, fps, dt * 1000.0);
                glfwSetWindowTitle(window, title.c_str());
            }
        }

        glfwTerminate();
    }

    return 0;
}
