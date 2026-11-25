#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

#include <glm/glm.hpp>
#include <array>

class DisneyEdsl final : public ExampleBase {
public:
    explicit DisneyEdsl(void* native_window);
    ~DisneyEdsl() override = default;
    void run() override;

private:
    struct Varyings {
        EmbeddedShader::Float3 v_view;
        EmbeddedShader::Float3 v_normal;
    };

    struct DrawParameters {
        glm::mat4 model{1.0f};
        glm::vec4 baseColorMetallic;
        float roughness = 0.5f, specular = 0.5f, specularTint = 0.0f, subsurface = 0.0f;
        float anisotropic = 0.0f, sheen = 0.0f, sheenTint = 0.5f, clearcoat = 0.0f;
        float clearcoatGloss = 1.0f;
    };

    static constexpr int rows = 10, columns = 11;
    HardwareDisplayer display;
    EmbeddedShader::TextureCube<ktm::fvec4> radiance_map, irradiance_map;
    RasterGeometry sphere;
    std::array<DrawParameters, rows * columns> draws;
    EmbeddedShader::Float4x4 proj_view;
    EmbeddedShader::Float4 camPos, lightDir;
    EmbeddedShader::Texture2D<ktm::fvec4> final_output;
    RasterizerPipeline<> rasterizer;
    HardwareExecutor render_executor;
};
