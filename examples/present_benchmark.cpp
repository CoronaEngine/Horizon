#include "present_benchmark.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

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
        const double mean = std::accumulate(samples_.begin(), samples_.end(), 0.0) / limit_;
        std::sort(samples_.begin(), samples_.end());
        const double p95 = samples_[static_cast<size_t>(std::ceil(limit_ * 0.95)) - 1];
        std::cout << "PRESENT_BENCHMARK," << mode_ << ",frames=" << limit_
                  << ",mean_ms=" << mean << ",p95_ms=" << p95 << ",fps=" << 1000.0 / mean << '\n';
    }
}
}
