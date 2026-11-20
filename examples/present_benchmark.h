#pragma once

#include <chrono>
#include <vector>

struct GLFWwindow;

namespace legacy_example {
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
}
