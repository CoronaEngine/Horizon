#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

#include <glm/vec4.hpp>
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
    glm::vec4 axis_projection;
    std::array<glm::vec4, dim> translations;
    EmbeddedShader::Texture2D<ktm::fvec4> output;
    RasterizerPipeline<> rasterizer;
    HardwareExecutor render_executor;
};
