#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(Disney/Slang/disney_vert.slang)
#include SLANG(Disney/Slang/disney_frag.slang)

#include <cstddef>
#include <ktm/ktm.h>

class DisneySlang final : public ExampleBase {
public:
    explicit DisneySlang(void* native_window);
    ~DisneySlang() override = default;
    void run(float) override;

private:
    struct alignas(16) DrawParameters {
        ktm::fmat4x4 model = ktm::fmat4x4::from_eye();
        ktm::fvec4 baseColorMetallic;
        float roughness = 0.5f, specular = 0.5f, specularTint = 0.0f, subsurface = 0.0f;
        float anisotropic = 0.0f, sheen = 0.0f, sheenTint = 0.5f, clearcoat = 0.0f;
        float clearcoatGloss = 1.0f;
    };
    // DisneyDraw in Slang reads three float4 material groups after the model matrix.
    static_assert(offsetof(DrawParameters, baseColorMetallic) == 64 &&
                  offsetof(DrawParameters, roughness) == 80 &&
                  offsetof(DrawParameters, anisotropic) == 96 &&
                  offsetof(DrawParameters, clearcoatGloss) == 112);

    static constexpr int rows = 10, columns = 11;

    HardwareDisplayer display;
    HardwareImage radiance, irradiance;
    HardwareBuffer vertex_buffer, index_buffer, commands, parameter_buffer;
    HardwareImage final_output_image, depth_image;
    RasterPipeline<disney_vert_slang_t, disney_frag_slang_t> pipeline;
    HardwareExecutor render_executor;
};
