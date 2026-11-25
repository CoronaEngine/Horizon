#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

#include <ktm/ktm.h>
#include <array>
#include <cstdint>

class DrawStressEdsl final : public ExampleBase {
public:
    explicit DrawStressEdsl(void* native_window);
    ~DrawStressEdsl() override = default;
    void run(float seconds) override;

private:
    static constexpr int dim = 40;
    static constexpr uint32_t draw_count = dim * dim * dim;
    HardwareDisplayer display;
    HardwareBuffer vertex_buffer, index_buffer;
    HardwareBuffer commands;
    ktm::fvec4 axis_projection;
    std::array<ktm::fvec4, dim> translations;
    EmbeddedShader::Texture2D<ktm::fvec4> output;
    RasterizerPipeline<> rasterizer;
    HardwareExecutor render_executor;
};
