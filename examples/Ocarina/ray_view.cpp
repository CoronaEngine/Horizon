#include "ray_view.h"
#include "window.h"
#include "example_benchmark/benchmark.h"
#include "Horizon.h"
#include <GLFW/glfw3.h>
#include <stb_image_write.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace ray_view {
struct Display {
    GLFWwindow* window;
    bench::PresentSamples samples;
    HardwareDisplayer surface;
    HardwareExecutor copy;
    HardwareBuffer staging{width * height * 4, BufferUsage::StorageBuffer};
    HardwareImage image{width, height, ImageFormat::RGBA8_SRGB, ImageUsage::SampledImage};
    double start = glfwGetTime();
    float fixed_dt{};
    uint64_t frame{};
};

DisplayPtr open(GLFWwindow* window, const char* mode) {
    DisplayPtr view{
        new Display{window, bench::present_samples(mode, mode),
                    HardwareDisplayer(examples::native_handle(window), PresentDevicePreference::RenderDevice)},
        [](Display* p) {
            p->copy.waitForDeferredResources();
            delete p;
        }};
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        view->fixed_dt = std::stof(value);
        if (!std::isfinite(view->fixed_dt) || view->fixed_dt < 0)
            throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }
    return view;
}

float elapsed_seconds(const Display& view) {
    return view.fixed_dt ? static_cast<float>(view.frame) * view.fixed_dt
                         : static_cast<float>(glfwGetTime() - view.start);
}

void present(Display& view, std::span<const uint32_t> pixels) {
    // The compute device and Horizon presenter can be on different adapters.
    // Wait before reusing the upload buffer that the previous frame still reads.
    view.copy.waitForDeferredResources();
    if (pixels.size() != width * height || !view.staging.copyFromData(pixels.data(), pixels.size_bytes()))
        throw std::runtime_error("Ray image upload failed");
    view.copy << view.staging.copyTo(view.image);
    const bool shown = view.surface.present(view.copy, view.image);
    bench::observe_present(view.samples, view.window, shown);
    ++view.frame;
    if (glfwWindowShouldClose(view.window)) {
        if (const char* path = std::getenv("HORIZON_CAPTURE_PATH"); path && *path) {
            const auto parent = std::filesystem::path(path).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);
            if (!stbi_write_png(path, width, height, 4, pixels.data(), width * 4))
                throw std::runtime_error("Could not write ray-traced PNG");
        }
    }
}
} // namespace ray_view
