#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

class BaselineEdsl final : public ExampleBase {
public:
    explicit BaselineEdsl(void* native_window);
    ~BaselineEdsl() override = default;
    void run(float seconds) override;

private:
    struct VertexInput {
        EmbeddedShader::Float3 pos;
        EmbeddedShader::Float2 tex_coord;
    };

    HardwareDisplayer display;
    HardwareBuffer vertex_buffer, index_buffer, commands;
    EmbeddedShader::Texture2D<ktm::fvec4> albedo, color;
    EmbeddedShader::Float4x4 model, view, proj;
    RasterizerPipeline<> rasterizer;
    HardwareExecutor render_executor;
};
