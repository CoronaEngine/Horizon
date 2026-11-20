#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

struct GLFWwindow;
struct HardwareExecutor;
struct HardwareDisplayer;
struct HardwareImage;
class RasterBatch;

namespace bench {
using Clock = std::chrono::steady_clock;

int env_int(const char* name, int fallback);
double milliseconds_since(Clock::time_point start);

struct PresentSamples {
    const char* mode;
    int limit, warmup;
    std::vector<double> intervals;
    Clock::time_point previous{};
    const char* window_title{};
    Clock::time_point last_title_update{};
    unsigned title_frames{};
};

PresentSamples present_samples(const char* mode, const char* window_title = nullptr);
void observe_present(PresentSamples&, GLFWwindow*, bool presented);

struct RunDetails;
struct Run {
    PresentSamples presents;
    bool present_normally;
    float elapsed_seconds{}, delta_seconds{};
    std::unique_ptr<RunDetails, void (*)(RunDetails*)> details;
};

// Disney and DrawStress can also measure or capture a bounded sequence of frames.
Run start_run(const char* mode, uint32_t width, uint32_t height, double shader_setup_ms);
void begin_frame(Run&);
void submit_measured(Run&, Clock::time_point record_start, HardwareExecutor&, RasterBatch&&,
                     HardwareDisplayer&, const HardwareImage&);
void finish_frame(Run&, GLFWwindow*, uint64_t draws, const HardwareImage&, HardwareExecutor&);
} // namespace bench
