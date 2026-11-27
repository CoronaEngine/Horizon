#pragma once

#include "../../ExampleBase.h"
#include "../DrawStressTransforms.h"
#include "Horizon.h"
#include "Codegen/TypeAlias.h"

#include <ktm/ktm.h>

class DrawStressEdsl final : public ExampleBase {
public:
    explicit DrawStressEdsl(void* native_window);
    ~DrawStressEdsl() override = default;
    void run(float seconds) override;

private:
    HardwareDisplayer display;
    HardwareBuffer vertex_buffer, index_buffer;
    HardwareBuffer commands;
    DrawStressTransforms transforms;
    HardwareImage output_image, depth_image;
    EmbeddedShader::Texture2D<ktm::fvec4> output{output_image};
    RasterPipeline<> raster;
    HardwareExecutor render_executor;
};
