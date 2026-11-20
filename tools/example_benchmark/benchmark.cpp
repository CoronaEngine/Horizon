#include "benchmark.h"
#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/packing.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace bench {
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

double milliseconds_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

PresentSamples present_samples(const char* mode, const char* title) {
    const int limit = env_int("HORIZON_PRESENT_FRAMES", 0);
    PresentSamples samples{mode, limit, limit ? env_int("HORIZON_BENCH_WARMUP", 60) : 0};
    samples.intervals.reserve(limit);
    samples.window_title = title;
    samples.last_title_update = Clock::now();
    return samples;
}

void observe_present(PresentSamples& samples, GLFWwindow* window, bool presented) {
    const auto now = Clock::now();
    if (samples.window_title) {
        ++samples.title_frames;
        const double seconds = std::chrono::duration<double>(now - samples.last_title_update).count();
        if (seconds >= 0.5) {
            const double fps = samples.title_frames / seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "%s %.1f FPS (%.2f ms)", samples.window_title, fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            samples.last_title_update = now;
            samples.title_frames = 0;
        }
    }
    if (!samples.limit) return;
    if (presented) {
        const auto last = std::exchange(samples.previous, now);
        if (last != Clock::time_point{}) {
            if (samples.warmup > 0) --samples.warmup;
            else samples.intervals.push_back(std::chrono::duration<double, std::milli>(now - last).count());
        }
    }
    if (samples.intervals.size() == static_cast<size_t>(samples.limit)) {
        const auto [mean, p95] = frame_stats(samples.intervals);
        std::cout << "PRESENT_BENCHMARK," << samples.mode << ",frames=" << samples.limit
                  << ",mean_ms=" << mean << ",p95_ms=" << p95 << ",fps=" << 1000.0 / mean << '\n';
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if (glfwWindowShouldClose(window)) {
        throw std::runtime_error("Window closed before the present benchmark finished");
    }
}

struct GpuTimer {
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

struct RunDetails {
    uint32_t width{}, height{};
    int warmup{}, limit{};
    uint64_t frame{}, title_frames{};
    bool offscreen{};
    float fixed_dt{};
    Clock::time_point start = Clock::now(), frame_start = start;
    double title_seconds{}, shader_setup_ms{}, first_submit_ms{};
    double record_ms{}, submit_ms{}, gpu_ms{}, total_record_ms{}, total_submit_ms{}, total_gpu_ms{};
    std::vector<double> samples;
    std::unique_ptr<GpuTimer> gpu_timer;
};

Run start_run(const char* mode, uint32_t width, uint32_t height, double shader_setup_ms) {
    auto presents = present_samples(mode);
    auto details = std::make_unique<RunDetails>();
    details->width = width;
    details->height = height;
    details->shader_setup_ms = shader_setup_ms;
    details->warmup = env_int("HORIZON_BENCH_WARMUP", 60);
    details->limit = env_int("HORIZON_BENCH_FRAMES", 0);
    details->offscreen = env_int("HORIZON_BENCH_OFFSCREEN", 0) != 0;
    const bool present_normally = details->limit == 0 && !details->offscreen;
    if (presents.limit && !present_normally)
        throw std::invalid_argument("HORIZON_PRESENT_FRAMES requires normal windowed presentation");
    if (details->limit) details->gpu_timer = std::make_unique<GpuTimer>();
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        details->fixed_dt = std::stof(value);
        if (!std::isfinite(details->fixed_dt) || details->fixed_dt < 0)
            throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }
    std::cout << "[HorizonExamples] " << mode << " " << width << 'x' << height
              << "; indirect BDA raster; serialized=" << bool(details->gpu_timer)
              << "; offscreen=" << details->offscreen << "; warmup=" << details->warmup
              << "; measured_frames=" << details->limit << '\n';
    return {std::move(presents), present_normally, 0, 0,
            {details.release(), [](RunDetails* ptr) { delete ptr; }}};
}

void begin_frame(Run& run) {
    auto& stats = *run.details;
    const auto previous = std::exchange(stats.frame_start, Clock::now());
    run.delta_seconds = stats.fixed_dt > 0 ? stats.fixed_dt : std::chrono::duration<float>(stats.frame_start - previous).count();
    run.elapsed_seconds = stats.fixed_dt > 0 ? stats.fixed_dt * static_cast<float>(stats.frame)
                             : std::chrono::duration<float>(stats.frame_start - stats.start).count();
}

void submit_measured(Run& run, Clock::time_point record_start, HardwareExecutor& executor, RasterBatch&& batch,
                     HardwareDisplayer& display, const HardwareImage& image) {
    auto& stats = *run.details;
    const auto start = Clock::now();
    stats.record_ms = std::chrono::duration<double, std::milli>(start - record_start).count();
    auto* backend = reinterpret_cast<HardwareExecutorVulkan*>(executor.getExecutorID());
    const auto previous_point = backend->getSubmissionPoint();
    if (stats.gpu_timer) *backend << &stats.gpu_timer->begin;
    executor << std::move(batch);
    if (stats.gpu_timer) *backend << &stats.gpu_timer->end;
    executor.commit();
    stats.submit_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (stats.frame == 0) stats.first_submit_ms = stats.submit_ms;
    if (stats.gpu_timer) {
        // Wait for this exact submission, including its trailing timestamp.
        const auto point = backend->getSubmissionPoint();
        if (!point || (point.queue == previous_point.queue && point.value == previous_point.value))
            throw std::runtime_error("Benchmark submission failed");
        VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.semaphoreCount = 1;
        wait.pSemaphores = &point.queue->timelineSemaphore;
        wait.pValues = &point.value;
        coronaHardwareCheck(vkWaitSemaphores(stats.gpu_timer->device, &wait, 30'000'000'000ULL));
        stats.gpu_ms = stats.gpu_timer->read();
        if (!stats.offscreen) display.wait(executor) << image;
    }
}

void finish_frame(Run& run, GLFWwindow* window, uint64_t draws, const HardwareImage& image, HardwareExecutor& ready) {
    auto& stats = *run.details;
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - stats.frame_start).count();
    if (stats.limit > 0 && stats.frame >= static_cast<uint64_t>(stats.warmup)) {
        stats.samples.push_back(ms);
        stats.total_record_ms += stats.record_ms;
        stats.total_submit_ms += stats.submit_ms;
        stats.total_gpu_ms += stats.gpu_ms;
    }
    ++stats.frame;
    stats.title_seconds += ms / 1000.0;
    ++stats.title_frames;
    if (stats.title_seconds >= 0.5) {
        const std::string title = "Horizon " + std::string(run.presents.mode) + " - " + std::to_string(draws)
            + " draws - " + std::to_string(stats.title_frames / stats.title_seconds) + " FPS";
        glfwSetWindowTitle(window, title.c_str());
        stats.title_seconds = 0;
        stats.title_frames = 0;
    }
    const bool finished = stats.limit > 0 && stats.samples.size() >= static_cast<size_t>(stats.limit);
    if (finished || glfwWindowShouldClose(window)) {
        ready.waitForDeferredResources();
        if (!stats.samples.empty()) {
            const auto [mean, p95] = frame_stats(stats.samples);
            const auto count = stats.samples.size();
            std::cout << "BENCHMARK," << run.presents.mode << ",frames=" << count << ",draws=" << draws
                      << ",mean_ms=" << mean << ",p95_ms=" << p95 << ",fps=" << 1000.0 / mean
                      << ",cpu_record_ms=" << stats.total_record_ms / count << ",cpu_submit_ms=" << stats.total_submit_ms / count
                      << ",gpu_ms=" << stats.total_gpu_ms / count << ",shader_setup_ms=" << stats.shader_setup_ms
                      << ",first_submit_ms=" << stats.first_submit_ms << ",offscreen=" << stats.offscreen << '\n';
        }
        if (const char* path = std::getenv("HORIZON_CAPTURE_PATH"); path && *path)
            capture(image, ready, stats.width, stats.height, path);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}
} // namespace bench
