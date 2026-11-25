#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

#include <ktm/ktm.h>
#include <array>
#include <chrono>
#include <cstdint>

class DrawStressEdsl final : public ExampleBase {
public:
    explicit DrawStressEdsl(void* native_window);
    ~DrawStressEdsl() override = default;
    void run() override;

private:
    static constexpr int dim = 40;
    static constexpr uint32_t draw_count = dim * dim * dim;
    HardwareDisplayer display;
    std::chrono::steady_clock::time_point start;
    std::uint64_t frame = 0;
    RasterGeometry cube;
    HardwareBuffer commands;
    ktm::fvec4 axis_projection;
    std::array<ktm::fvec4, dim> translations;
    EmbeddedShader::Texture2D<ktm::fvec4> output;
    RasterizerPipeline<> rasterizer;
    HardwareExecutor render_executor;
};
