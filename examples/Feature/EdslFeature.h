#pragma once

#include "../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

class EdslFeature final : public ExampleBase {
public:
    explicit EdslFeature(void* native_window);
    void run(float seconds) override;

private:
    static constexpr uint32_t width = 800, height = 600, group_size = 8;

    struct VertexInput {
        EmbeddedShader::Float3 position, normal;
    };

    HardwareBuffer vertex_buffer, index_buffer, commands;
    HardwareImage albedo_image{width, height, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage};
    HardwareImage normal_image{width, height, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage};
    HardwareImage shaded_image{width, height, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage};
    EmbeddedShader::Texture2D<ktm::fvec4> albedo{albedo_image}, normals{normal_image}, shaded{shaded_image};
    EmbeddedShader::Float4x4 view_projection;
    RasterizerPipeline<> geometry;
    ComputePipeline<> lighting;
    HardwareExecutor executor;
    HardwareDisplayer display;
};
