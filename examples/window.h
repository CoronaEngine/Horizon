#pragma once

#include <memory>

struct GLFWwindow;

namespace examples {
using WindowPtr = std::unique_ptr<GLFWwindow, void (*)(GLFWwindow*)>;

WindowPtr open_window(int width, int height, const char* title, bool resizable = false);
void* native_handle(GLFWwindow* window);
} // namespace examples
