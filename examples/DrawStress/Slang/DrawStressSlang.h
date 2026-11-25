#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <ktm/ktm.h>

class DrawStressSlang final : public ExampleBase {
public:
    explicit DrawStressSlang(void* native_window);
    ~DrawStressSlang() override = default;
    void run() override;

private:
    static constexpr int dim = 40;
    static constexpr uint32_t draw_count = dim * dim * dim;

    std::chrono::steady_clock::time_point start;
    std::uint64_t frame = 0;
    HardwareDisplayer display;
    RasterGeometry cube;
    HardwareBuffer commands;
    ktm::fvec4 axis_projection;
    std::array<ktm::fvec4, dim> translations;
    HardwareImage final_output_image;
    RasterizerPipeline<> rasterizer;
    HardwareExecutor render_executor;
};
