#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ktm/ktm.h>

class BaselineSlang final : public ExampleBase {
public:
    explicit BaselineSlang(void* native_window);
    ~BaselineSlang() override = default;
    void run() override;

private:
    std::chrono::steady_clock::time_point start;
    std::uint64_t frame = 0;
    HardwareDisplayer display;
    HardwareBuffer vertex_buffer, index_buffer, commands;
    HardwareImage texture_image;
    HardwareImage color;
    RasterizerPipeline<> rasterizer;
    struct DrawParameters {
        ktm::fmat4x4 model{};
        uint32_t texIndex{};
    } parameters{};
    static_assert(offsetof(DrawParameters, texIndex) == 64);
    HardwareExecutor render_executor;
};
