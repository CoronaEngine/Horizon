#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

#include <ktm/ktm.h>
#include <array>
#include <cstddef>
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
    std::array<HardwareBuffer, 2> parameter_buffers;
    std::size_t next_parameter_buffer{};
    ktm::fvec4 axis_projection;
    std::array<ktm::fvec4, dim> translations;
    HardwareImage output_image;
    EmbeddedShader::Texture2D<ktm::fvec4> output{output_image};
    RasterizerPipeline<> rasterizer;
    HardwareExecutor render_executor;
};
