#pragma once

#include "Horizon.h"
#include <glm/mat4x4.hpp>
#include <chrono>
#include <cstdint>

struct GLFWwindow;

namespace baseline {
constexpr uint32_t width = 1920;
constexpr uint32_t height = 1080;

struct Scene {
    Scene();
    RasterGeometry geometry;
    HardwareImage texture, color, depth;
    glm::mat4 view, projection;
};

class Animation {
public:
    glm::mat4 next_model(GLFWwindow* window, const char* title);

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_ = Clock::now();
    Clock::time_point title_updated_ = start_;
    int frames_ = 0;
};
} // namespace baseline
