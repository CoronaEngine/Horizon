#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;
struct HardwareExecutor;
struct HardwareDisplayer;
struct HardwareImage;
class RasterBatch;

namespace examples {
int env_int(const char* name, int fallback);

// Sample intervals between successful presents on the normal windowed render path.
class PresentBenchmark {
public:
    explicit PresentBenchmark(const char* mode);
    bool enabled() const { return limit_ > 0; }
    bool running(GLFWwindow* window) const;
    void presented(bool success);

private:
    using Clock = std::chrono::steady_clock;
    const char* mode_;
    int limit_;
    int warmup_;
    Clock::time_point previous_{};
    std::vector<double> samples_;
};

class Window {
public:
    Window(uint32_t width, uint32_t height, const char* title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    GLFWwindow* get() const { return window_; }
    void* native_handle() const;
private:
    GLFWwindow* window_{};
};

// Bounded runs measure serialized scene submissions with Vulkan timestamps.
// Animation uses an independent fixed dt; offscreen runs skip presentation.
class Benchmark {
public:
    Benchmark(const char* mode, uint32_t width, uint32_t height);
    ~Benchmark();
    bool running(GLFWwindow* window) const { return present_benchmark_.running(window); }
    void begin_frame();
    void begin_scene();
    void submit(HardwareExecutor&, RasterBatch&&, HardwareDisplayer&, const HardwareImage&);
    void set_shader_setup_ms(double value) { shader_setup_ms_ = value; }
    float dt() const;
    float time() const;
    void end_frame(GLFWwindow*, uint64_t draws, const HardwareImage&, HardwareExecutor&);
private:
    using Clock = std::chrono::steady_clock;
    std::string mode_;
    PresentBenchmark present_benchmark_;
    uint32_t width_, height_;
    int warmup_, limit_;
    uint64_t frame_{};
    float fixed_dt_{};
    Clock::time_point start_, frame_start_;
    double dt_{}, title_seconds_{};
    uint64_t title_frames_{};
    std::vector<double> samples_;
    struct GpuTimer;
    std::unique_ptr<GpuTimer> gpu_timer_;
    bool offscreen_{};
    Clock::time_point scene_start_;
    double shader_setup_ms_{}, first_submit_ms_{};
    double record_ms_{}, submit_ms_{}, gpu_ms_{};
    double total_record_ms_{}, total_submit_ms_{}, total_gpu_ms_{};
};
} // namespace examples
