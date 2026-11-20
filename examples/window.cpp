#include "window.h"
#include "example_benchmark/benchmark.h"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <stdexcept>

namespace examples {
WindowPtr open_window(int width, int height, const char* title, bool resizable) {
    const bool hidden = bench::env_int("HORIZON_BENCH_HIDDEN", 0) != 0;
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) { glfwTerminate(); throw std::runtime_error("glfwCreateWindow failed"); }
    glfwSetWindowSizeLimits(window, width, height, GLFW_DONT_CARE, GLFW_DONT_CARE);
    if (resizable)
        glfwSetWindowSizeLimits(window, GLFW_DONT_CARE, GLFW_DONT_CARE, GLFW_DONT_CARE, GLFW_DONT_CARE);
    else
        glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
    return {window, [](GLFWwindow* handle) {
        glfwDestroyWindow(handle);
        glfwTerminate();
    }};
}

void* native_handle(GLFWwindow* window) {
#if defined(_WIN32)
    return glfwGetWin32Window(window);
#elif defined(__APPLE__)
    return glfwGetCocoaView(window);
#else
    throw std::runtime_error("Native Horizon window handles are unavailable on this platform");
#endif
}
} // namespace examples
