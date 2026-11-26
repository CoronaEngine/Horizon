#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

#include <ktm/ktm.h>

class DisneyEdsl final : public ExampleBase {
public:
    explicit DisneyEdsl(void* native_window);
    ~DisneyEdsl() override = default;
    void run(float) override;

private:
    struct Varyings {
        EmbeddedShader::Float3 v_view;
        EmbeddedShader::Float3 v_normal;
    };

    struct alignas(16) DrawParameters {
        ktm::fmat4x4 model = ktm::fmat4x4::from_eye();
        ktm::fvec4 baseColorMetallic;
        float roughness = 0.5f, specular = 0.5f, specularTint = 0.0f, subsurface = 0.0f;
        float anisotropic = 0.0f, sheen = 0.0f, sheenTint = 0.5f, clearcoat = 0.0f;
        float clearcoatGloss = 1.0f;
    };

    static constexpr int rows = 10, columns = 11;
    HardwareDisplayer display;
    HardwareImage radiance_image, irradiance_image, final_output_image;
    EmbeddedShader::TextureCube<ktm::fvec4> radiance_map{radiance_image}, irradiance_map{irradiance_image};
    HardwareBuffer vertex_buffer, index_buffer, commands, parameter_buffer;
    EmbeddedShader::Float4x4 proj_view;
    EmbeddedShader::Float4 camPos, lightDir;
    EmbeddedShader::Texture2D<ktm::fvec4> final_output{final_output_image};
    RasterPipeline<> raster;
    HardwareExecutor render_executor;
};
