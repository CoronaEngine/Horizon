#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"

#include <array>
#include <glm/glm.hpp>

class DisneySlang final : public ExampleBase {
public:
    explicit DisneySlang(void* native_window);
    ~DisneySlang() override = default;
    void run() override;

private:
    struct DrawParameters {
        glm::mat4 model{1.0f};
        glm::vec4 baseColorMetallic;
        float roughness = 0.5f, specular = 0.5f, specularTint = 0.0f, subsurface = 0.0f;
        float anisotropic = 0.0f, sheen = 0.0f, sheenTint = 0.5f, clearcoat = 0.0f;
        float clearcoatGloss = 1.0f;
    };

    static constexpr int rows = 10, columns = 11;

    HardwareDisplayer display;
    HardwareImage radiance, irradiance;
    RasterGeometry sphere;
    std::array<DrawParameters, rows * columns> draws;
    HardwareImage final_output_image;
    RasterizerPipeline<> rasterizer;
    HardwareExecutor render_executor;
};
