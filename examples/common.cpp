#include "common.h"
#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <glm/gtc/packing.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace examples {
namespace {
struct FrameStats {
    double mean, p95;
};

FrameStats frame_stats(std::vector<double>& samples) {
    const double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    std::sort(samples.begin(), samples.end());
    return {mean, samples[static_cast<size_t>(std::ceil(samples.size() * 0.95)) - 1]};
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
} // namespace

int env_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    char* end{};
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end || parsed < 0 || parsed > 10000000)
        throw std::invalid_argument(std::string("Invalid ") + name);
    return static_cast<int>(parsed);
}

PresentBenchmark::PresentBenchmark(const char* mode)
    : mode_(mode), limit_(env_int("HORIZON_PRESENT_FRAMES", 0)),
      warmup_(enabled() ? env_int("HORIZON_BENCH_WARMUP", 60) : 0) {
    samples_.reserve(limit_);
}

bool PresentBenchmark::running(GLFWwindow* window) const {
    if (enabled() && samples_.size() == static_cast<size_t>(limit_)) return false;
    if (!glfwWindowShouldClose(window)) return true;
    if (enabled()) throw std::runtime_error("Window closed before the present benchmark finished");
    return false;
}

void PresentBenchmark::presented(bool success) {
    if (!enabled() || !success) return;
    const auto now = Clock::now();
    const auto last = std::exchange(previous_, now);
    if (last == Clock::time_point{}) return;
    if (warmup_ > 0) {
        --warmup_;
        return;
    }
    samples_.push_back(std::chrono::duration<double, std::milli>(now - last).count());
    if (samples_.size() == static_cast<size_t>(limit_)) {
        const auto [mean, p95] = frame_stats(samples_);
        std::cout << "PRESENT_BENCHMARK," << mode_ << ",frames=" << limit_
                  << ",mean_ms=" << mean << ",p95_ms=" << p95 << ",fps=" << 1000.0 / mean << '\n';
    }
}

struct Benchmark::GpuTimer {
    struct Marker : CommandRecordVulkan {
        GpuTimer* timer{};
        uint32_t index{};
        void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList&) override {}
        ExecutorType getExecutorType() override { return ExecutorType::Graphics; }
        void commitCommand(HardwareExecutorVulkan& executor) override {
            const auto* queue = executor.currentRecordQueue;
            const auto cb = queue->commandBuffer;
            timer->valid_bits = timer->queue_properties.at(queue->queueFamilyIndex).timestampValidBits;
            if (!timer->valid_bits) throw std::runtime_error("Selected queue does not support timestamps");
            if (index == 0) vkCmdResetQueryPool(cb, timer->pool, 0, 2);
            vkCmdWriteTimestamp2(cb, index ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                 timer->pool, index);
        }
    } begin, end;
    VkDevice device{};
    VkQueryPool pool{};
    float period{};
    uint32_t valid_bits{};
    std::vector<VkQueueFamilyProperties> queue_properties;
    GpuTimer() {
        auto context = getHardwareContext().getMainDevice();
        device = context->deviceManager.getLogicalDevice();
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context->deviceManager.getPhysicalDevice(), &properties);
        period = properties.limits.timestampPeriod;
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(context->deviceManager.getPhysicalDevice(), &queue_count, nullptr);
        queue_properties.resize(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(context->deviceManager.getPhysicalDevice(), &queue_count, queue_properties.data());
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 2;
        coronaHardwareCheck(vkCreateQueryPool(device, &info, nullptr, &pool));
        begin.timer = end.timer = this;
        end.index = 1;
#ifdef CABBAGE_ENGINE_DEBUG
        constexpr bool engine_debug = true;
#else
        constexpr bool engine_debug = false;
#endif
        std::cout << "BENCH_DEVICE,name=" << properties.deviceName << ",vendor=" << properties.vendorID
                  << ",device=" << properties.deviceID << ",driver=" << properties.driverVersion
                  << ",timestamp_period_ns=" << period << ",engine_debug=" << engine_debug << '\n';
    }
    ~GpuTimer() { vkDestroyQueryPool(device, pool, nullptr); }
    double read() {
        uint64_t ticks[2]{};
        coronaHardwareCheck(vkGetQueryPoolResults(device, pool, 0, 2, sizeof(ticks), ticks, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        const uint64_t mask = valid_bits == 64 ? UINT64_MAX : (uint64_t{1} << valid_bits) - 1;
        return static_cast<double>((ticks[1] - ticks[0]) & mask) * period / 1e6;
    }
};

Window::Window(uint32_t width, uint32_t height, const char* title) {
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    if (env_int("HORIZON_BENCH_HIDDEN", 0)) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) { glfwTerminate(); throw std::runtime_error("glfwCreateWindow failed"); }
    glfwSetWindowSizeLimits(window_, width, height, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowAttrib(window_, GLFW_RESIZABLE, GLFW_FALSE);
}
Window::~Window() { glfwDestroyWindow(window_); glfwTerminate(); }

void* Window::native_handle() const {
#if defined(_WIN32)
    return glfwGetWin32Window(window_);
#elif defined(__APPLE__)
    return glfwGetCocoaView(window_);
#else
    throw std::runtime_error("Native Horizon window handles are unavailable on this platform");
#endif
}

Benchmark::Benchmark(const char* mode, uint32_t width, uint32_t height)
    : mode_(mode), present_benchmark_(mode), width_(width), height_(height),
      warmup_(env_int("HORIZON_BENCH_WARMUP", 60)), limit_(env_int("HORIZON_BENCH_FRAMES", 0)),
      start_(Clock::now()), frame_start_(start_) {
    offscreen_ = env_int("HORIZON_BENCH_OFFSCREEN", 0) != 0;
    if (present_benchmark_.enabled() && (limit_ || offscreen_))
        throw std::invalid_argument("HORIZON_PRESENT_FRAMES requires normal windowed presentation");
    if (limit_ > 0) gpu_timer_ = std::make_unique<GpuTimer>();
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        fixed_dt_ = std::stof(value);
        if (!std::isfinite(fixed_dt_) || fixed_dt_ < 0) throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }
    std::cout << "[HorizonExamples] " << mode_ << " " << width_ << 'x' << height_
              << "; indirect BDA raster; serialized=" << bool(gpu_timer_)
              << "; offscreen=" << offscreen_ << "; warmup=" << warmup_
              << "; measured_frames=" << limit_ << '\n';
}
Benchmark::~Benchmark() = default;
void Benchmark::begin_scene() { scene_start_ = Clock::now(); }
void Benchmark::submit(HardwareExecutor& executor, RasterBatch&& batch, HardwareDisplayer& display, const HardwareImage& image) {
    const auto start = Clock::now();
    record_ms_ = std::chrono::duration<double, std::milli>(start - scene_start_).count();
    auto* backend = reinterpret_cast<HardwareExecutorVulkan*>(executor.getExecutorID());
    const auto previous_point = backend->getSubmissionPoint();
    if (gpu_timer_) *backend << &gpu_timer_->begin;
    executor << std::move(batch);
    if (gpu_timer_) *backend << &gpu_timer_->end;
    if (gpu_timer_ || offscreen_) executor.commit();
    else present_benchmark_.presented(display.present(executor, image));
    submit_ms_ = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (frame_ == 0) first_submit_ms_ = submit_ms_;
    if (gpu_timer_) {
        // Wait for this exact submission, including its trailing timestamp.
        const auto point = backend->getSubmissionPoint();
        if (!point || (point.queue == previous_point.queue && point.value == previous_point.value))
            throw std::runtime_error("Benchmark submission failed");
        VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.semaphoreCount = 1;
        wait.pSemaphores = &point.queue->timelineSemaphore;
        wait.pValues = &point.value;
        coronaHardwareCheck(vkWaitSemaphores(gpu_timer_->device, &wait, 30'000'000'000ULL));
        gpu_ms_ = gpu_timer_->read();
        if (!offscreen_) display.wait(executor) << image;
    }
}
void Benchmark::begin_frame() {
    const auto previous = std::exchange(frame_start_, Clock::now());
    dt_ = std::chrono::duration<double>(frame_start_ - previous).count();
}
float Benchmark::dt() const { return fixed_dt_ > 0 ? fixed_dt_ : static_cast<float>(dt_); }
float Benchmark::time() const {
    return fixed_dt_ > 0 ? fixed_dt_ * static_cast<float>(frame_)
                        : std::chrono::duration<float>(frame_start_ - start_).count();
}
void Benchmark::end_frame(GLFWwindow* window, uint64_t draws, const HardwareImage& image, HardwareExecutor& ready) {
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - frame_start_).count();
    if (limit_ > 0 && frame_ >= static_cast<uint64_t>(warmup_)) {
        samples_.push_back(ms);
        total_record_ms_ += record_ms_;
        total_submit_ms_ += submit_ms_;
        total_gpu_ms_ += gpu_ms_;
    }
    ++frame_;
    title_seconds_ += ms / 1000.0;
    ++title_frames_;
    if (title_seconds_ >= 0.5) {
        const std::string title = "Horizon " + mode_ + " - " + std::to_string(draws)
            + " draws - " + std::to_string(title_frames_ / title_seconds_) + " FPS";
        glfwSetWindowTitle(window, title.c_str());
        title_seconds_ = 0;
        title_frames_ = 0;
    }
    const bool finished = limit_ > 0 && samples_.size() >= static_cast<size_t>(limit_);
    if (finished || glfwWindowShouldClose(window)) {
        ready.waitForDeferredResources();
        if (!samples_.empty()) {
            const auto [mean, p95] = frame_stats(samples_);
            const auto count = samples_.size();
            std::cout << "BENCHMARK," << mode_ << ",frames=" << count << ",draws=" << draws
                      << ",mean_ms=" << mean << ",p95_ms=" << p95 << ",fps=" << 1000.0 / mean
                      << ",cpu_record_ms=" << total_record_ms_ / count << ",cpu_submit_ms=" << total_submit_ms_ / count
                      << ",gpu_ms=" << total_gpu_ms_ / count << ",shader_setup_ms=" << shader_setup_ms_
                      << ",first_submit_ms=" << first_submit_ms_ << ",offscreen=" << offscreen_ << '\n';
        }
        if (const char* path = std::getenv("HORIZON_CAPTURE_PATH"); path && *path)
            capture(image, ready, width_, height_, path);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}
} // namespace examples
