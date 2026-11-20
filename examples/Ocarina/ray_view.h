#pragma once

#include <cstdint>
#include <memory>
#include <span>

struct GLFWwindow;

namespace ray_view {
constexpr uint32_t width = 960, height = 540;
struct Display;
using DisplayPtr = std::unique_ptr<Display, void (*)(Display*)>;
DisplayPtr open(GLFWwindow* window, const char* mode);
float elapsed_seconds(const Display& display);
void present(Display& display, std::span<const uint32_t> rgba);
} // namespace ray_view
