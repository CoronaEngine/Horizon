#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

#include <chrono>
#include <cstdint>

class BaselineEdsl final : public ExampleBase {
public:
    explicit BaselineEdsl(void* native_window);
    ~BaselineEdsl() override = default;
    void run() override;

private:
    struct VertexInput {
        EmbeddedShader::Float3 pos;
        EmbeddedShader::Float2 tex_coord;
    };

    HardwareDisplayer display;
    std::chrono::steady_clock::time_point start;
    std::uint64_t frame = 0;
    HardwareBuffer vertex_buffer, index_buffer, commands;
    EmbeddedShader::Texture2D<ktm::fvec4> albedo, color;
    EmbeddedShader::Float4x4 model, view, proj;
    RasterizerPipeline<> rasterizer;
    HardwareExecutor render_executor;
};
