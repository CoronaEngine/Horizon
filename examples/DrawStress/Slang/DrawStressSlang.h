#pragma once

#include "../../ExampleBase.h"
#include "../DrawStressTransforms.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(DrawStress/Slang/drawstress_vert.slang)
#include SLANG(DrawStress/Slang/drawstress_frag.slang)

#include <ktm/ktm.h>

class DrawStressSlang final : public ExampleBase {
public:
    explicit DrawStressSlang(void* native_window);
    ~DrawStressSlang() override = default;
    void run(float seconds) override;

private:
    HardwareDisplayer display;
    HardwareBuffer vertex_buffer, index_buffer;
    HardwareBuffer commands;
    DrawStressTransforms transforms;
    HardwareImage final_output_image, depth_image;
    RasterPipeline<drawstress_vert_slang_t, drawstress_frag_slang_t> pipeline;
    HardwareExecutor render_executor;
};
