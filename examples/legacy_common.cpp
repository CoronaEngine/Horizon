#include "legacy_common.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <glm/gtc/packing.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace legacy_example {
int env_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    char* end{};
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end || parsed < 0 || parsed > 10000000)
        throw std::invalid_argument(std::string("Invalid ") + name);
    return static_cast<int>(parsed);
}
Window::Window(uint32_t width, uint32_t height, const char* title) {
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    if (env_int("HORIZON_BENCH_HIDDEN", 0)) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) { glfwTerminate(); throw std::runtime_error("glfwCreateWindow failed"); }
}
Window::~Window() { glfwDestroyWindow(window_); glfwTerminate(); }

Benchmark::Benchmark(const char* mode, uint32_t width, uint32_t height)
    : mode_(mode), width_(width), height_(height),
      warmup_(env_int("HORIZON_BENCH_WARMUP", 60)), limit_(env_int("HORIZON_BENCH_FRAMES", 0)),
      start_(Clock::now()), previous_(start_) {
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        fixed_dt_ = std::stof(value);
        if (!std::isfinite(fixed_dt_) || fixed_dt_ < 0) throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }
    std::cout << "[legacy] " << mode_ << " " << width_ << 'x' << height_
              << "; direct indexed draws; UI=" << HORIZON_IMGUI_RENDER_ENABLED
              << "; wall-clock timings include waits/present; warmup=" << warmup_
              << "; measured_frames=" << limit_ << '\n';
}
void Benchmark::begin_frame() {
    frame_start_ = Clock::now();
    dt_ = std::chrono::duration<double>(frame_start_ - previous_).count();
    previous_ = frame_start_;
}
float Benchmark::dt() const { return fixed_dt_ > 0 ? fixed_dt_ : static_cast<float>(dt_); }
float Benchmark::time() const {
    return fixed_dt_ > 0 ? fixed_dt_ * static_cast<float>(frame_)
                        : std::chrono::duration<float>(frame_start_ - start_).count();
}
void Benchmark::end_frame(GLFWwindow* window, uint64_t draws, const HardwareImage& image, HardwareExecutor& ready) {
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - frame_start_).count();
    if (limit_ > 0 && frame_ >= static_cast<uint64_t>(warmup_)) samples_.push_back(ms);
    ++frame_;
    title_seconds_ += ms / 1000.0;
    ++title_frames_;
    if (title_seconds_ >= 0.5) {
        const std::string title = "Horizon " + mode_ + " [Legacy] - " + std::to_string(draws)
            + " draws - " + std::to_string(title_frames_ / title_seconds_) + " FPS";
        glfwSetWindowTitle(window, title.c_str());
        title_seconds_ = 0;
        title_frames_ = 0;
    }
    const bool finished = limit_ > 0 && samples_.size() >= static_cast<size_t>(limit_);
    if (finished || glfwWindowShouldClose(window)) {
        ready.waitForDeferredResources();
        if (!samples_.empty()) {
            const double mean = std::accumulate(samples_.begin(), samples_.end(), 0.0) / samples_.size();
            auto sorted = samples_;
            std::sort(sorted.begin(), sorted.end());
            const double p95 = sorted[static_cast<size_t>(std::ceil(sorted.size() * 0.95)) - 1];
            std::cout << "BENCHMARK," << mode_ << ",frames=" << samples_.size() << ",draws=" << draws
                      << ",mean_ms=" << mean << ",p95_ms=" << p95 << ",fps=" << 1000.0 / mean << '\n';
        }
        if (const char* path = std::getenv("HORIZON_CAPTURE_PATH"); path && *path)
            capture(image, ready, width_, height_, path);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}
void capture(const HardwareImage& image, HardwareExecutor& ready, uint32_t width, uint32_t height,
             const std::filesystem::path& path) {
    const uint32_t byte_count = width * height * 8;
    HardwareBuffer readback(byte_count, BufferUsage::StorageBuffer);
    HardwareExecutor copy;
    copy.wait(ready);
    copy << image.copyTo(readback);
    copy.commit().waitForDeferredResources();
    std::vector<uint16_t> half(static_cast<size_t>(width) * height * 4);
    if (!readback.copyToData(half.data(), byte_count)) throw std::runtime_error("Capture readback failed");
    std::vector<unsigned char> rgba(half.size());
    for (size_t i = 0; i < half.size(); ++i)
        rgba[i] = static_cast<unsigned char>(std::clamp(glm::unpackHalf1x16(half[i]), 0.0f, 1.0f) * 255.0f + 0.5f);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    if (!stbi_write_png(path.string().c_str(), width, height, 4, rgba.data(), width * 4))
        throw std::runtime_error("Capture PNG write failed");
    std::cout << "Capture: " << path.string() << '\n';
}
}
