#pragma once
#include "Horizon_old.h"
#include "HardwareCommands_old.h"
#include <GLFW/glfw3.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace legacy_example {
int env_int(const char* name, int fallback);
class Window {
public:
    Window(uint32_t width, uint32_t height, const char* title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    GLFWwindow* get() const { return window_; }
private:
    GLFWwindow* window_{};
};

// Bounded runs measure serialized scene submissions with Vulkan timestamps.
// Animation uses an independent fixed dt; offscreen runs skip presentation.
class Benchmark {
public:
    Benchmark(const char* mode, uint32_t width, uint32_t height);
    ~Benchmark();
    void begin_frame();
    void begin_scene();
    void submit(HardwareExecutor&, RasterBatch&&);
    bool offscreen() const { return offscreen_; }
    void set_shader_setup_ms(double value) { shader_setup_ms_ = value; }
    float dt() const;
    float time() const;
    void end_frame(GLFWwindow*, uint64_t draws, const HardwareImage&, HardwareExecutor&);
private:
    using Clock = std::chrono::steady_clock;
    std::string mode_;
    uint32_t width_, height_;
    int warmup_, limit_;
    uint64_t frame_{};
    float fixed_dt_{};
    Clock::time_point start_, previous_, frame_start_;
    double dt_{}, title_seconds_{};
    uint64_t title_frames_{};
    std::vector<double> samples_;
    struct GpuTimer;
    std::unique_ptr<GpuTimer> gpu_timer_;
    bool offscreen_{};
    Clock::time_point scene_start_;
    double shader_setup_ms_{}, first_submit_ms_{};
    double record_ms_{}, submit_ms_{}, gpu_ms_{};
    std::vector<double> record_samples_, submit_samples_, gpu_samples_;
};

void capture(const HardwareImage&, HardwareExecutor&, uint32_t width, uint32_t height,
             const std::filesystem::path& path);
}
